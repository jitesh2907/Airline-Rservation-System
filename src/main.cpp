#include "crow_all.h"
#include <sqlite3.h>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <csignal>
#include <mutex>

// Thread-safe mutex for SQLite database operations
static std::mutex g_db_mutex;

struct Edge {
    std::string to_airport;
    std::string flight_id;
    double price;
};

struct RouteResult {
    double total_cost;
    std::vector<std::string> path;
    std::vector<std::string> flight_ids;
};

// C++ Graph Dijkstra Shortest Path Engine
class FlightGraph {
public:
    std::unordered_map<std::string, std::vector<Edge>> adj;

    void addFlight(const std::string& from, const std::string& to, const std::string& id, double price) {
        adj[from].push_back({to, id, price});
    }

    RouteResult findCheapestRoute(const std::string& start, const std::string& target) {
        if (start == target) {
            return {0.0, {start}, {}};
        }

        using CostNode = std::pair<double, std::string>;
        std::priority_queue<CostNode, std::vector<CostNode>, std::greater<CostNode>> pq;
        std::unordered_map<std::string, double> dist;
        std::unordered_map<std::string, std::pair<std::string, std::string>> parent;

        pq.push({0.0, start});
        dist[start] = 0.0;

        while (!pq.empty()) {
            auto [cost, u] = pq.top();
            pq.pop();

            if (u == target) break;
            if (cost > dist[u]) continue;

            auto it = adj.find(u);
            if (it == adj.end()) continue;

            for (const auto& edge : it->second) {
                double new_cost = cost + edge.price;
                if (!dist.count(edge.to_airport) || new_cost < dist[edge.to_airport]) {
                    dist[edge.to_airport] = new_cost;
                    parent[edge.to_airport] = {u, edge.flight_id};
                    pq.push({new_cost, edge.to_airport});
                }
            }
        }

        RouteResult res{dist.count(target) ? dist[target] : -1.0, {}, {}};
        if (res.total_cost < 0) return res;

        std::string curr = target;
        while (curr != start) {
            res.path.push_back(curr);
            res.flight_ids.push_back(parent[curr].second);
            curr = parent[curr].first;
        }
        res.path.push_back(start);
        std::reverse(res.path.begin(), res.path.end());
        std::reverse(res.flight_ids.begin(), res.flight_ids.end());
        return res;
    }
};

void initDatabase(sqlite3* db) {
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS bookings ("
        "  booking_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  flight_id TEXT NOT NULL,"
        "  passenger_name TEXT NOT NULL,"
        "  seat_number TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(flight_id, seat_number)"
        ");";
    char* err = nullptr;
    std::lock_guard<std::mutex> lock(g_db_mutex);
    sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (err) {
        std::cerr << "SQLite initialization error: " << err << "\n";
        sqlite3_free(err);
    }
}

inline void add_cors_headers(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin", "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS, PUT");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin, X-Requested-With");
}

int main() {
    // Suppress SIGPIPE so client disconnects don't terminate the process
    std::signal(SIGPIPE, SIG_IGN);

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2("airline.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    initDatabase(db);

    // Initialize Crow with built-in CORS middleware
    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global()
        .headers("Content-Type", "Authorization", "Accept", "Origin", "X-Requested-With")
        .methods(crow::HTTPMethod::Post, crow::HTTPMethod::Get, crow::HTTPMethod::Delete, crow::HTTPMethod::Options)
        .origin("*");

    FlightGraph graph;
    // Network Graph: Major Indian Metro Air Hubs (DEL, BOM, BLR)
    graph.addFlight("DEL", "BOM", "AI101", 4500.0);
    graph.addFlight("BOM", "BLR", "6E202", 3200.0);
    graph.addFlight("DEL", "BLR", "UK808", 8500.0);
    graph.addFlight("BOM", "DEL", "AI102", 4500.0);
    graph.addFlight("BLR", "BOM", "6E203", 3200.0);
    graph.addFlight("BLR", "DEL", "UK809", 8500.0);

    // Explicit OPTIONS routes to ensure every browser preflight passes cleanly
    CROW_ROUTE(app, "/api/<path>").methods(crow::HTTPMethod::Options)
    ([](std::string) {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/book").methods(crow::HTTPMethod::Options)
    ([]() {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/bookings").methods(crow::HTTPMethod::Options)
    ([]() {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/bookings/<int>").methods(crow::HTTPMethod::Options)
    ([](int) {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/flights").methods(crow::HTTPMethod::Options)
    ([]() {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/flights/optimize").methods(crow::HTTPMethod::Options)
    ([]() {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/api/flights/<string>/seats").methods(crow::HTTPMethod::Options)
    ([](std::string) {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    // 1. Health & Diagnostics Check
    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue resp;
        resp["status"] = "running";
        resp["engine"] = "Crow C++17 (Optimized)";
        resp["port"] = 18080;
        resp["database"] = "SQLite3 Thread-Safe";
        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 2. Flight Catalog
    CROW_ROUTE(app, "/api/flights")
    ([]() {
        crow::json::wvalue resp = crow::json::wvalue::list();
        
        resp[0]["flight_id"] = "AI101";
        resp[0]["airline"] = "Air India";
        resp[0]["airline_code"] = "AI";
        resp[0]["from"] = "DEL";
        resp[0]["from_name"] = "New Delhi (DEL)";
        resp[0]["to"] = "BOM";
        resp[0]["to_name"] = "Mumbai (BOM)";
        resp[0]["price"] = 4500.0;
        resp[0]["departure"] = "06:00 AM";
        resp[0]["arrival"] = "08:15 AM";
        resp[0]["duration"] = "2h 15m";
        resp[0]["aircraft"] = "Airbus A320neo";

        resp[1]["flight_id"] = "6E202";
        resp[1]["airline"] = "IndiGo";
        resp[1]["airline_code"] = "6E";
        resp[1]["from"] = "BOM";
        resp[1]["from_name"] = "Mumbai (BOM)";
        resp[1]["to"] = "BLR";
        resp[1]["to_name"] = "Bengaluru (BLR)";
        resp[1]["price"] = 3200.0;
        resp[1]["departure"] = "10:30 AM";
        resp[1]["arrival"] = "12:15 PM";
        resp[1]["duration"] = "1h 45m";
        resp[1]["aircraft"] = "Airbus A321neo";

        resp[2]["flight_id"] = "UK808";
        resp[2]["airline"] = "Vistara";
        resp[2]["airline_code"] = "UK";
        resp[2]["from"] = "DEL";
        resp[2]["from_name"] = "New Delhi (DEL)";
        resp[2]["to"] = "BLR";
        resp[2]["to_name"] = "Bengaluru (BLR)";
        resp[2]["price"] = 8500.0;
        resp[2]["departure"] = "07:15 AM";
        resp[2]["arrival"] = "10:05 AM";
        resp[2]["duration"] = "2h 50m";
        resp[2]["aircraft"] = "Boeing 787-9 Dreamliner";

        resp[3]["flight_id"] = "AI102";
        resp[3]["airline"] = "Air India";
        resp[3]["airline_code"] = "AI";
        resp[3]["from"] = "BOM";
        resp[3]["from_name"] = "Mumbai (BOM)";
        resp[3]["to"] = "DEL";
        resp[3]["to_name"] = "New Delhi (DEL)";
        resp[3]["price"] = 4500.0;
        resp[3]["departure"] = "06:30 PM";
        resp[3]["arrival"] = "08:45 PM";
        resp[3]["duration"] = "2h 15m";
        resp[3]["aircraft"] = "Airbus A320neo";

        resp[4]["flight_id"] = "6E203";
        resp[4]["airline"] = "IndiGo";
        resp[4]["airline_code"] = "6E";
        resp[4]["from"] = "BLR";
        resp[4]["from_name"] = "Bengaluru (BLR)";
        resp[4]["to"] = "BOM";
        resp[4]["to_name"] = "Mumbai (BOM)";
        resp[4]["price"] = 3200.0;
        resp[4]["departure"] = "04:15 PM";
        resp[4]["arrival"] = "06:00 PM";
        resp[4]["duration"] = "1h 45m";
        resp[4]["aircraft"] = "Airbus A321neo";

        resp[5]["flight_id"] = "UK809";
        resp[5]["airline"] = "Vistara";
        resp[5]["airline_code"] = "UK";
        resp[5]["from"] = "BLR";
        resp[5]["from_name"] = "Bengaluru (BLR)";
        resp[5]["to"] = "DEL";
        resp[5]["to_name"] = "New Delhi (DEL)";
        resp[5]["price"] = 8500.0;
        resp[5]["departure"] = "11:15 AM";
        resp[5]["arrival"] = "02:05 PM";
        resp[5]["duration"] = "2h 50m";
        resp[5]["aircraft"] = "Boeing 787-9 Dreamliner";

        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 3. Dijkstra Route Optimization
    CROW_ROUTE(app, "/api/flights/optimize")
    ([&graph](const crow::request& req) {
        auto from = req.url_params.get("from");
        auto to = req.url_params.get("to");

        if (!from || !to) {
            crow::response res(400, "{\"error\": \"Provide both 'from' and 'to' parameters.\"}");
            add_cors_headers(res);
            return res;
        }

        RouteResult res_route = graph.findCheapestRoute(from, to);
        if (res_route.total_cost < 0) {
            crow::response res(404, "{\"error\": \"No route found.\"}");
            add_cors_headers(res);
            return res;
        }

        crow::json::wvalue resp;
        resp["total_cost"] = res_route.total_cost;
        for (size_t i = 0; i < res_route.path.size(); ++i) resp["path"][i] = res_route.path[i];
        for (size_t i = 0; i < res_route.flight_ids.size(); ++i) resp["flights"][i] = res_route.flight_ids[i];
        
        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 4. Get Occupied Seats for a Flight
    CROW_ROUTE(app, "/api/flights/<string>/seats")
    ([db](std::string flight_id) {
        std::vector<std::string> occupied;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            std::string query = "SELECT seat_number FROM bookings WHERE flight_id = ?;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                crow::response res(500, "{\"error\": \"Failed to query seats.\"}");
                add_cors_headers(res);
                return res;
            }

            sqlite3_bind_text(stmt, 1, flight_id.c_str(), -1, SQLITE_STATIC);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* seat = sqlite3_column_text(stmt, 0);
                if (seat) occupied.push_back(reinterpret_cast<const char*>(seat));
            }
            sqlite3_finalize(stmt);
        }

        crow::json::wvalue resp = crow::json::wvalue::empty_object();
        resp["occupied_seats"] = crow::json::wvalue::list{};
        for (size_t i = 0; i < occupied.size(); ++i) {
            resp["occupied_seats"][i] = occupied[i];
        }
        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 4b. Get All Bookings
    CROW_ROUTE(app, "/api/bookings")
    ([db]() {
        crow::json::wvalue resp = crow::json::wvalue::list();
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            std::string query = "SELECT booking_id, flight_id, passenger_name, seat_number, created_at FROM bookings ORDER BY booking_id DESC LIMIT 100;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                crow::response res(500, "{\"error\": \"Failed to query bookings.\"}");
                add_cors_headers(res);
                return res;
            }

            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                resp[idx]["booking_id"] = sqlite3_column_int(stmt, 0);
                const unsigned char* f = sqlite3_column_text(stmt, 1);
                const unsigned char* p = sqlite3_column_text(stmt, 2);
                const unsigned char* s = sqlite3_column_text(stmt, 3);
                const unsigned char* c = sqlite3_column_text(stmt, 4);
                resp[idx]["flight_id"] = f ? reinterpret_cast<const char*>(f) : "";
                resp[idx]["passenger_name"] = p ? reinterpret_cast<const char*>(p) : "";
                resp[idx]["seat_number"] = s ? reinterpret_cast<const char*>(s) : "";
                resp[idx]["created_at"] = c ? reinterpret_cast<const char*>(c) : "";
                idx++;
            }
            sqlite3_finalize(stmt);
        }

        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 4c. Cancel Booking
    CROW_ROUTE(app, "/api/bookings/<int>").methods(crow::HTTPMethod::Delete)
    ([db](int booking_id) {
        int rc = 0;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            std::string del_query = "DELETE FROM bookings WHERE booking_id = ?;";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, del_query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                crow::response res(500, "{\"error\": \"Database error.\"}");
                add_cors_headers(res);
                return res;
            }

            sqlite3_bind_int(stmt, 1, booking_id);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        if (rc != SQLITE_DONE) {
            crow::response res(500, "{\"error\": \"Failed to delete booking.\"}");
            add_cors_headers(res);
            return res;
        }

        crow::json::wvalue resp;
        resp["status"] = "CANCELLED";
        resp["booking_id"] = booking_id;
        crow::response res(200, resp);
        add_cors_headers(res);
        return res;
    });

    // 5. Book Seat with Collision Handling (Safe Prepared Statements)
    CROW_ROUTE(app, "/api/book").methods(crow::HTTPMethod::Post)
    ([db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("flight_id") || !body.has("passenger_name") || !body.has("seat_number")) {
            crow::response res(400, "{\"error\": \"Invalid JSON payload. Provide flight_id, passenger_name, and seat_number.\"}");
            add_cors_headers(res);
            return res;
        }

        std::string flight = body["flight_id"].s();
        std::string name = body["passenger_name"].s();
        std::string seat = body["seat_number"].s();

        int rc = 0;
        int new_id = 0;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            std::string insert_query = "INSERT INTO bookings (flight_id, passenger_name, seat_number) VALUES (?, ?, ?);";
            sqlite3_stmt* stmt = nullptr;

            if (sqlite3_prepare_v2(db, insert_query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                crow::response res(500, "{\"error\": \"Database prepare error.\"}");
                add_cors_headers(res);
                return res;
            }

            sqlite3_bind_text(stmt, 1, flight.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, seat.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE) {
                new_id = static_cast<int>(sqlite3_last_insert_rowid(db));
            }
            sqlite3_finalize(stmt);
        }

        if (rc == SQLITE_CONSTRAINT) {
            crow::response res(409, "{\"error\": \"Seat " + seat + " is already booked on flight " + flight + "!\"}");
            add_cors_headers(res);
            return res;
        } else if (rc != SQLITE_DONE) {
            crow::response res(500, "{\"error\": \"Failed to complete booking transaction.\"}");
            add_cors_headers(res);
            return res;
        }

        crow::json::wvalue resp;
        resp["status"] = "CONFIRMED";
        resp["flight_id"] = flight;
        resp["passenger"] = name;
        resp["seat"] = seat;
        resp["booking_id"] = new_id;
        crow::response res(201, resp);
        add_cors_headers(res);
        return res;
    });

    std::cout << "====================================================\n";
    std::cout << "SkyRoute Neo C++17 Crow API Server (Thread-Safe)\n";
    std::cout << "Dijkstra Engine & SQLite Collision-Safe Ledger Active\n";
    std::cout << "Port: 18080 | CORS: Enabled (*)\n";
    std::cout << "====================================================\n";

    app.loglevel(crow::LogLevel::Warning);
    app.port(18080).run();

    {
        std::lock_guard<std::mutex> lock(g_db_mutex);
        sqlite3_close(db);
    }
    return 0;
}
