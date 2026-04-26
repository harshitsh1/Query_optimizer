
// Minimal HTTP Server for Query Optimizer Web UI


// Windows socket headers MUST come before C++ std headers to avoid
// std::byte vs Windows byte conflict (MinGW)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include "optimizer.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

using namespace std;

// Forward declarations
extern "C" {
    #include "sql_parser.h"
    #include "y.tab.h"
    extern FILE *yyin;
    extern int yyparse(void);
    extern RelNode *result;
}

// We need a way to parse from string. We'll use a temp file approach on Windows.
extern "C" {
    typedef struct yy_buffer_state *YY_BUFFER_STATE;
    YY_BUFFER_STATE yy_scan_string(const char *str);
    void yy_delete_buffer(YY_BUFFER_STATE buf);
}

static string readFile(const string& path) {
    ifstream f(path);
    if (!f.is_open()) return "";
    ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static string getContentType(const string& path) {
    if (path.find(".html") != string::npos) return "text/html";
    if (path.find(".css") != string::npos)  return "text/css";
    if (path.find(".js") != string::npos)   return "application/javascript";
    if (path.find(".json") != string::npos) return "application/json";
    return "text/plain";
}

static string buildHttpResponse(int code, const string& contentType, const string& body) {
    ostringstream ss;
    ss << "HTTP/1.1 " << code << " OK\r\n"
       << "Content-Type: " << contentType << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n"
       << "\r\n"
       << body;
    return ss.str();
}

// Parse HTTP request to get method, path, and body
struct HttpRequest {
    string method;
    string path;
    string body;
};

static HttpRequest parseRequest(const string& raw) {
    HttpRequest req;
    istringstream stream(raw);
    stream >> req.method >> req.path;

    // Find body after \r\n\r\n
    auto pos = raw.find("\r\n\r\n");
    if (pos != string::npos) {
        req.body = raw.substr(pos + 4);
    }
    return req;
}


// Server function


void startServer(int port, Optimizer& optimizer) {
    cout << "Starting Query Optimizer server on http://localhost:" << port << endl;

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == INVALID_SOCKET) {
        cerr << "Failed to create socket" << endl;
        return;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Failed to bind to port " << port << endl;
        closesocket(serverSock);
        return;
    }

    listen(serverSock, 10);
    cout << "Server listening... Open http://localhost:" << port << " in your browser." << endl;

    while (true) {
        struct sockaddr_in clientAddr;
        int clientLen = sizeof(clientAddr);
        SOCKET clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSock == INVALID_SOCKET) continue;

        // Read request
        char buffer[65536];
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) {
            closesocket(clientSock);
            continue;
        }

        string rawRequest(buffer, bytesRead);
        HttpRequest req = parseRequest(rawRequest);

        string response;

        if (req.method == "OPTIONS") {
            // CORS preflight
            response = buildHttpResponse(200, "text/plain", "");
        }
        else if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
            string content = readFile("web/index.html");
            response = buildHttpResponse(200, "text/html", content);
        }
        else if (req.method == "GET" && req.path == "/style.css") {
            string content = readFile("web/style.css");
            response = buildHttpResponse(200, "text/css", content);
        }
        else if (req.method == "GET" && req.path == "/app.js") {
            string content = readFile("web/app.js");
            response = buildHttpResponse(200, "application/javascript", content);
        }
        else if (req.method == "GET" && req.path == "/api/catalog") {
            string json = optimizer.getCatalog().toJSON();
            response = buildHttpResponse(200, "application/json", json);
        }
        else if (req.method == "POST" && req.path == "/api/optimize") {
            // Parse SQL from request body and optimize
            string sql = req.body;
            // Trim whitespace
            while (!sql.empty() && (sql.front() == ' ' || sql.front() == '\n' || sql.front() == '\r'))
                sql.erase(sql.begin());

            // Extract SQL from JSON body: {"sql": "..."}
            auto sqlPos = sql.find("\"sql\"");
            if (sqlPos != string::npos) {
                // Find the colon after "sql"
                auto colonPos = sql.find(':', sqlPos + 4);
                if (colonPos != string::npos) {
                    // Find opening quote of the value
                    auto start = sql.find('"', colonPos + 1);
                    if (start != string::npos) {
                        start++; // skip opening quote
                        // Find closing quote (skip escaped quotes)
                        string extracted;
                        bool escaped = false;
                        for (size_t i = start; i < sql.size(); i++) {
                            if (escaped) {
                                // Unescape JSON escape sequences
                                switch (sql[i]) {
                                    case 'n':  extracted += '\n'; break;
                                    case 't':  extracted += '\t'; break;
                                    case 'r':  extracted += '\r'; break;
                                    case '"':  extracted += '"';  break;
                                    case '\\': extracted += '\\'; break;
                                    case '\'': extracted += '\''; break;
                                    default:   extracted += sql[i]; break;
                                }
                                escaped = false;
                            } else if (sql[i] == '\\') {
                                escaped = true;
                            } else if (sql[i] == '"') {
                                break; // end of JSON string value
                            } else {
                                extracted += sql[i];
                            }
                        }
                        sql = extracted;
                    }
                }
            }

            if (sql.empty()) {
                response = buildHttpResponse(400, "application/json",
                    "{\"error\":\"Empty SQL query\"}");
            } else {
                // Strip semicolons and trailing whitespace (lexer doesn't handle ';')
                while (!sql.empty() && (sql.back() == ';' || sql.back() == ' ' ||
                       sql.back() == '\n' || sql.back() == '\r' || sql.back() == '\t'))
                    sql.pop_back();

                // Parse SQL using flex/bison
                result = NULL;
                YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
                int parseResult = yyparse();
                yy_delete_buffer(buf);

                if (parseResult != 0 || result == NULL) {
                    response = buildHttpResponse(400, "application/json",
                        "{\"error\":\"Parse error in SQL query\"}");
                } else {
                    auto optResult = optimizer.optimize(result);
                    string json = optResult.toJSON();
                    free_relnode(result);
                    result = NULL;
                    response = buildHttpResponse(200, "application/json", json);
                }
            }
        }
        else {
            response = buildHttpResponse(404, "text/plain", "Not Found");
        }

        send(clientSock, response.c_str(), (int)response.size(), 0);
        closesocket(clientSock);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
