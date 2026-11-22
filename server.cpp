#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sqlite3.h>

// Helper to format money to 2 decimal places
std::string formatMoney(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount;
    return oss.str();
}

// --- CORE OOP + SQLITE DATABASE CLASS ---
class BankDatabase {
private:
    sqlite3* db;

    void executeSQL(const std::string& sql) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "[SQL ERROR] " << errMsg << "\n";
            sqlite3_free(errMsg);
        }
    }

public:
    BankDatabase(const std::string& dbFile) {
        if (sqlite3_open(dbFile.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database!\n";
            exit(1);
        }

        // Create tables if they do not exist yet
        executeSQL("CREATE TABLE IF NOT EXISTS account (id INTEGER PRIMARY KEY, balance REAL);");
        executeSQL("CREATE TABLE IF NOT EXISTS transactions (id INTEGER PRIMARY KEY AUTOINCREMENT, description TEXT);");

        // Initialize default balance of 1000.00 only on first run
        executeSQL("INSERT OR IGNORE INTO account (id, balance) VALUES (1, 1000.00);");

        // Add initial log only if transactions table is empty
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM transactions;", -1, &stmt, nullptr);
        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (count == 0) {
            addTransaction("Account opened with Rs. 1000.00");
        }
    }

    ~BankDatabase() {
        sqlite3_close(db);
    }

    double getBalance() {
        double bal = 0.0;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT balance FROM account WHERE id = 1;", -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            bal = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return bal;
    }

    void addTransaction(const std::string& desc) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "INSERT INTO transactions (description) VALUES (?);", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, desc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::string deposit(double amount) {
        if (amount <= 0) return "Error: Enter a valid positive amount.";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "UPDATE account SET balance = balance + ? WHERE id = 1;", -1, &stmt, nullptr);
        sqlite3_bind_double(stmt, 1, amount);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        addTransaction("Deposited +Rs. " + formatMoney(amount));
        return "Successfully deposited Rs. " + formatMoney(amount);
    }

    std::string withdraw(double amount) {
        if (amount <= 0) return "Error: Enter a valid positive amount.";
        if (amount > getBalance()) return "Error: Insufficient balance!";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "UPDATE account SET balance = balance - ? WHERE id = 1;", -1, &stmt, nullptr);
        sqlite3_bind_double(stmt, 1, amount);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        addTransaction("Withdrew -Rs. " + formatMoney(amount));
        return "Successfully withdrew Rs. " + formatMoney(amount);
    }

    std::string toJson(const std::string& statusMsg) {
        std::vector<std::string> history;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT description FROM transactions ORDER BY id ASC;", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            history.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);

        std::string json = "{";
        json += "\"balance\":\"" + formatMoney(getBalance()) + "\",";
        json += "\"message\":\"" + statusMsg + "\",";
        json += "\"history\":[";
        for (size_t i = 0; i < history.size(); ++i) {
            json += "\"" + history[i] + "\"";
            if (i + 1 < history.size()) json += ",";
        }
        json += "]}";
        return json;
    }
};

// Extract "?amt=500" from HTTP request
double extractAmount(const std::string& request) {
    size_t pos = request.find("amt=");
    if (pos == std::string::npos) return -1;
    return std::atof(request.substr(pos + 4).c_str());
}

// Read index.html so Codespaces can load the UI directly from port 8080
std::string readHtmlFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return "<h1>Error: index.html not found!</h1>";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// --- LINUX POSIX HTTP SERVER ---
int main() {
    BankDatabase db("bank.db"); // Connects to (or creates) SQLite database file

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    // Allow instant restart on port 8080 without "Address already in use" error
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    std::cout << "====================================================\n";
    std::cout << " C++ SQLite Bank Server running on port 8080...\n";
    std::cout << " Database file: bank.db\n";
    std::cout << "====================================================\n";

    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        char buffer[4096] = {0};
        read(clientSocket, buffer, sizeof(buffer) - 1);

        std::string request(buffer);
        std::string response;

        // Serve the Frontend HTML page on root URL "/"
        if (request.find("GET / ") != std::string::npos) {
            std::string html = readHtmlFile("index.html");
            response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: " + std::to_string(html.length()) + "\r\n\r\n" +
                html;
        }
        // Ignore browser favicon requests
        else if (request.find("GET /favicon.ico") != std::string::npos) {
            response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
        // API Routes talking to SQLite
        else {
            std::string statusMsg = "Connected to C++ & SQLite Backend";

            if (request.find("GET /deposit") != std::string::npos) {
                double amt = extractAmount(request);
                statusMsg = db.deposit(amt);
                std::cout << "[SQLITE] Deposit: Rs. " << amt << " -> " << statusMsg << "\n";
            }
            else if (request.find("GET /withdraw") != std::string::npos) {
                double amt = extractAmount(request);
                statusMsg = db.withdraw(amt);
                std::cout << "[SQLITE] Withdraw: Rs. " << amt << " -> " << statusMsg << "\n";
            }
            else if (request.find("GET /balance") != std::string::npos) {
                std::cout << "[SQLITE] Balance & History Fetched\n";
            }

            std::string jsonBody = db.toJson(statusMsg);
            response =
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(jsonBody.length()) + "\r\n\r\n" +
                jsonBody;
        }

        send(clientSocket, response.c_str(), response.length(), 0);
        close(clientSocket);
    }

    close(serverSocket);
    return 0;
}