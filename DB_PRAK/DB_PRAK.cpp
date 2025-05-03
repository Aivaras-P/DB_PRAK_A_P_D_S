#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mariadb/conncpp.hpp>
#include <crow.h>
#include <unordered_map>
#include <sstream>
#include <cstdlib>

// === Naudotojo klasės ===
class Naudotojas {
protected:
    std::string vardas;
public:
    Naudotojas(std::string vardas) : vardas(vardas) {}
    virtual std::string gautiRole() const = 0;
    virtual std::string gautiVarda() const { return vardas; }
    virtual ~Naudotojas() {}
};

class RegistruotasNaudotojas : public Naudotojas {
public:
    RegistruotasNaudotojas(std::string vardas) : Naudotojas(vardas) {}
    std::string gautiRole() const override { return "Registruotas"; }
};

class Administratorius : public Naudotojas {
public:
    Administratorius(std::string vardas) : Naudotojas(vardas) {}
    std::string gautiRole() const override { return "Administratorius"; }
};

// === Sesijų saugojimas ===
std::unordered_map<std::string, std::unique_ptr<Naudotojas>> aktyviosSesijos;

std::string generuotiSesijosID() {
    std::string id;
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    for (int i = 0; i < 20; ++i)
        id += chars[rand() % (sizeof(chars) - 1)];
    return id;
}

// === URL decode funkcija ===
std::string url_decode(const std::string& value) {
    std::string result;
    char ch;
    int i, ii;
    for (i = 0; i < value.length(); i++) {
        if (value[i] == '%') {
            sscanf_s(value.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            result += ch;
            i = i + 2;
        }
        else if (value[i] == '+') {
            result += ' ';
        }
        else {
            result += value[i];
        }
    }
    return result;
}

std::map<std::string, std::string> parseUrlEncoded(const std::string& body) {
    std::map<std::string, std::string> result;
    std::stringstream ss(body);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = pair.substr(0, eq);
            std::string value = pair.substr(eq + 1);
            result[url_decode(key)] = url_decode(value);
        }
    }

    return result;
}



int main() {
    crow::SimpleApp app;

    // === Pagrindinis puslapis ===
    CROW_ROUTE(app, "/")
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        std::ostringstream html;

        html << "<html><body><div style='text-align:right'>";
        if (aktyviosSesijos.count(session_id)) {
            auto& naudotojas = aktyviosSesijos[session_id];
            html << "Prisijunges kaip: " << naudotojas->gautiVarda() << " (" << naudotojas->gautiRole() << ") ";
            html << "<a href='/logout'>Atsijungti</a>";
            html << "<div style='margin-top:10px;'>";

            html << "<form action='/mano_skelbimai' method='get' style='display:inline-block; margin-right:10px;'>";
            html << "<button type='submit'>Mano skelbimai</button>";
            html << "</form>";

            html << "<form action='/isiminti_skelbimai' method='get' style='display:inline-block;'>";
            html << "<button type='submit'>Isiminti skelbimai</button>";
            html << "</form>";
        }
        else {
            html << "<a href='/login'>Prisijungti</a>";
        }


        html << "</div>";

        // === Paieškos forma ===
        html << R"(
<form method="GET" action="/search">
    <input type="text" name="query" placeholder="Iveskite zaidimo pavadinima">
    <input type="submit" value="Ieskoti">
</form>
)";


        html << "<h1 style='text-align:center;'>Sveiki atvyke!</h1></body></html>";
        return html.str();
            });

    // === Prisijungimo forma ===
    CROW_ROUTE(app, "/login").methods("GET"_method)
        ([] {
        return R"(
            <html><body>
<button onclick='window.history.back()' style = "margin-bottom: 10px;"> Atgal</button>
            <h2>Prisijungimas</h2>
            <form method='POST' action='/login'>
                Vartotojo vardas: <input name='username'><br>
                Slaptazodis: <input type='password' name='password'><br>
                <input type='submit' value='Prisijungti'>
            </form>
            </body></html>
        )";
            });

    // === Prisijungimo logika ===
    CROW_ROUTE(app, "/login").methods("POST"_method)
        ([](const crow::request& req) {
        auto post_data = parseUrlEncoded(req.body);
        std::string username = post_data["username"];
        std::string password = post_data["password"];

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT Role FROM naudotojas WHERE Prisijungimo_Vardas=? AND Slaptazodis=?"));
            stmt->setString(1, username);
            stmt->setString(2, password);

            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            if (res->next()) {
                std::string role = static_cast<std::string>(res->getString("Role"));

                std::string session_id = generuotiSesijosID();
                if (role == "Administratorius") {
                    aktyviosSesijos[session_id] = std::make_unique<Administratorius>(username);
                }
                else if (role == "Registruotas") {
                    aktyviosSesijos[session_id] = std::make_unique<RegistruotasNaudotojas>(username);
                }
                else {
                    return crow::response(403, "Tik registruoti vartotojai gali prisijungti.");
                }

                crow::response resp;
                resp.code = 302;
                resp.add_header("Set-Cookie", session_id + "; Path=/");
                resp.add_header("Location", "/");
                return resp;
            }
        }
        catch (const sql::SQLException& e) {
            return crow::response(500, "DB klaida");
        }

        return crow::response(403, "Netinkami prisijungimo duomenys");
            });

    // === Atsijungimas ===
    CROW_ROUTE(app, "/logout")
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        aktyviosSesijos.erase(session_id);
        crow::response resp;
        resp.code = 302;
        resp.add_header("Location", "/");
        return resp;
            });

    CROW_ROUTE(app, "/search").methods("GET"_method, "POST"_method)
        ([](const crow::request& req) {
        std::string query;

        // Gauti paieškos užklausą pagal metodą
        if (req.method == crow::HTTPMethod::GET) {
            query = req.url_params.get("query") ? req.url_params.get("query") : "";
        }
        else if (req.method == crow::HTTPMethod::POST) {
            auto post_data = parseUrlEncoded(req.body);
            query = post_data["query"];
        }

        std::ostringstream html;
        html << "<html><body>";
        html << "<h2>Rezultatai pagal: " << query << "</h2>";

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            // Rasti žaidimo ID pagal dalinį pavadinimą
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT ZaidimoID, Pavadinimas FROM zaidimas WHERE Pavadinimas LIKE ?"));
            stmt->setString(1, "%" + query + "%");
            std::unique_ptr<sql::ResultSet> zaidimai(stmt->executeQuery());

            bool rasta = false;
            while (zaidimai->next()) {
                int zaidimoId = zaidimai->getInt("ZaidimoID");
                std::string pavadinimas = std::string(zaidimai->getString("Pavadinimas"));

                html << "<h3>Skelbimai zaidimui: " << pavadinimas << "</h3>";

                std::unique_ptr<sql::PreparedStatement> skelb_stmt(
                    conn->prepareStatement("SELECT * FROM skelbimas WHERE ZaidimoID=?"));
                skelb_stmt->setInt(1, zaidimoId);
                std::unique_ptr<sql::ResultSet> skelbimas(skelb_stmt->executeQuery());

                bool yraSkelbimu = false;
                while (skelbimas->next()) {
                    yraSkelbimu = true;
                    int skelbimoId = skelbimas->getInt("SkelbimoID");
                    double kaina = skelbimas->getDouble("Kaina");
                    std::string aprasymas = std::string(skelbimas->getString("Aprasymas"));
                    int kiekis = skelbimas->getInt("Zaidimo_Kiekis");

                    html << "<a href='/skelbimas?id=" << skelbimoId << "'>";
                    html << "<div style='border:1px solid black; margin:10px; padding:10px'>";
                    html << "<strong>Kaina:</strong> " << kaina << " €<br>";
                    html << "<strong>Kiekis:</strong> " << kiekis << "<br>";
                    html << "<strong>Aprasymas:</strong><br>" << aprasymas << "<br>";
                    html << "</div></a>";
                }

                if (!yraSkelbimu) {
                    html << "<p>Skelbimu siam zaidimui nera.</p>";
                }

                rasta = true;
            }

            if (!rasta) {
                html << "<p>zaidimu pagal sia uzklausa nerasta.</p>";
            }
        }
        catch (const sql::SQLException& e) {
            html << "<p>Klaida jungiantis prie duomenu bazes.</p>";
        }

        html << "<a href='/'>Grizti i pradzia</a>";
        html << "</body></html>";
        return html.str();
            });

            CROW_ROUTE(app, "/skelbimas")
                ([](const crow::request& req) {
                std::string id_str = req.url_params.get("id") ? req.url_params.get("id") : "";
                if (id_str.empty()) {
                    return crow::response(400, "Nenurodytas skelbimo ID.");
                }

                std::ostringstream html;
                html << "<html><body>";

                try {
                    int skelbimoId = std::stoi(id_str);  // Patikriname, ar ID galime konvertuoti į integer

                    sql::Driver* driver = sql::mariadb::get_driver_instance();
                    sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
                    sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
                    std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

                    // Gauti skelbimo informaciją
                    std::unique_ptr<sql::PreparedStatement> stmt(
                        conn->prepareStatement("SELECT * FROM skelbimas WHERE SkelbimoID=?"));
                    stmt->setInt(1, skelbimoId);
                    std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

                    if (!res || !res->next()) {
                        return crow::response(404, "Skelbimas nerastas.");
                    }

                    double kaina = res->getDouble("Kaina");
                    std::string aprasymas = std::string(res->getString("Aprasymas"));
                    int kiekis = res->getInt("Zaidimo_Kiekis");
                    int zaidimoId = res->getInt("ZaidimoID");
                    html << "<h2>Skelbimo informacija</h2>";
                    html << "<p><strong>Kaina:</strong> " << kaina << " €</p>";
                    html << "<p><strong>Kiekis:</strong> " << kiekis << "</p>";
                    html << "<p><strong>Aprasymas:</strong> " << aprasymas << "</p>";

            // Gauti session_id ir patikrinti, ar naudotojas prisijungęs
            std::string session_id = req.get_header_value("Cookie");
            if (aktyviosSesijos.count(session_id)) {
                auto& naudotojas = aktyviosSesijos[session_id];
                std::string vardas = naudotojas->gautiVarda();

                // Gauti naudotojo ID pagal vardą
                std::unique_ptr<sql::PreparedStatement> user_stmt(
                    conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
                user_stmt->setString(1, vardas);
                std::unique_ptr<sql::ResultSet> user_res(user_stmt->executeQuery());

                int naudotojo_id = -1;
                if (user_res->next()) {
                    naudotojo_id = user_res->getInt("NaudotojasID");
                }

                bool yra_skelbejas = false;
                std::unique_ptr<sql::PreparedStatement> creator_stmt(
                    conn->prepareStatement("SELECT NaudotojasID FROM skelbimas WHERE SkelbimoID=?"));
                creator_stmt->setInt(1, skelbimoId);
                std::unique_ptr<sql::ResultSet> creator_res(creator_stmt->executeQuery());
                if (creator_res->next()) {
                    int skelbejas_id = creator_res->getInt("NaudotojasID");
                    if (skelbejas_id == naudotojo_id) {
                        yra_skelbejas = true;
                    }
                }

                // Patikrinti, ar skelbimas jau įsimintas
                std::unique_ptr<sql::PreparedStatement> check_stmt(
                    conn->prepareStatement("SELECT COUNT(*) FROM isimintinas_skelbimas WHERE IsimintinoNaudotojoID=? AND SkelbimoID=?"));
                check_stmt->setInt(1, naudotojo_id);
                check_stmt->setInt(2, skelbimoId);  // Čia tiksliai perduodame SkelbimoID
                std::unique_ptr<sql::ResultSet> check_res(check_stmt->executeQuery());

                std::string role = naudotojas->gautiRole();
                bool yra_adminas = (role == "Administratorius");

                if (yra_skelbejas) {
                    // Redaguoti ir trinti
                    html << R"(
<div id='redagavimoForma' style='display:none; margin-top:15px;'>
  <h3>Redaguoti skelbima</h3>
  <form id='editForm' method='post' action='/redaguoti_skelbima'>
    <input type='hidden' name='SkelbimoID' value=')" << skelbimoId << R"('>
    <label>Kaina:</label><input type='number' step='0.01' name='Kaina' value=')" << kaina << R"(' required><br>
    <label>Kiekis:</label><input type='number' name='Zaidimo_Kiekis' value=')" << kiekis << R"(' required><br>
    <label>Aprašymas:</label><br><textarea name='Aprasymas'>)" << aprasymas << R"(</textarea><br>
    <button type='button' onclick='patvirtintiPakeitimus()'>Pakeisti</button>
    <button type='button' onclick='window.location.reload()'>Atsaukti</button>
  </form>
</div>

<script>
function rodytiRedagavimoForma() {
    document.getElementById('redagavimoForma').style.display = 'block';
}

function patvirtintiPakeitimus() {
    if (confirm("Ar tikrai norite issaugoti pakeitimus?")) {
        document.getElementById('editForm').submit();
    }
}

function istrintiSkelbima(id) {
    if (confirm("Ar tikrai norite istrinti si skelbima?")) {
        window.location.href = "/istrinti_skelbima?id=" + id;
    }
}
</script>
)";
                    html << "<button onclick=\"rodytiRedagavimoForma()\">Redaguoti</button> ";
                    html << "<button onclick=\"istrintiSkelbima(" << skelbimoId << ")\">Istrinti</button><br><br>";
                }
                else {
                    // Paprasti naudotojai IR administratorius – rodo Įsiminti ir galimybę trinti (adminui)
                    if (check_res->next() && check_res->getInt(1) > 0) {
                        html << "<form action='/panaikinti_is_isiminimo' method='post'>";
                        html << "<input type='hidden' name='SkelbimoID' value='" << skelbimoId << "'>";
                        html << "<button type='submit'>Panaikinti is isimintu</button>";
                        html << "</form>";
                    }
                    else {
                        html << "<form action='/isiminti' method='post'>";
                        html << "<input type='hidden' name='SkelbimoID' value='" << skelbimoId << "'>";
                        html << "<button type='submit'>Isiminti skelbima</button>";
                        html << "</form>";
                    }

                    // Jeigu administratorius – dar pridedam „Ištrinti“
                    if (yra_adminas) {
                        html << R"(
<script>
function istrintiSkelbima(id) {
    if (confirm("Ar tikrai norite istrinti si skelbima?")) {
        window.location.href = "/istrinti_skelbima?id=" + id;
    }
}
</script>
)";
                        html << "<button onclick=\"istrintiSkelbima(" << skelbimoId << ")\" style=\"background-color:red; color:white; margin-top:10px;\">Istrinti skelbima</button>";
                    }
                }
            }

            // Žaidimo informacijos gavimas
            std::unique_ptr<sql::PreparedStatement> game_stmt(
                conn->prepareStatement("SELECT * FROM zaidimas WHERE ZaidimoID=?"));
            game_stmt->setInt(1, zaidimoId);
            std::unique_ptr<sql::ResultSet> game_res(game_stmt->executeQuery());

            if (game_res->next()) {
                std::string pavadinimas = std::string(game_res->getString("Pavadinimas"));
                std::string kurejas = std::string(game_res->getString("Kurejas"));
                std::string leidejas = std::string(game_res->getString("Leidejas"));
                std::string isleidimo_data = std::string(game_res->getString("Isleidimo_Data"));
                std::string zanras = std::string(game_res->getString("Zanras"));

                html << "<h3>Zaidimo informacija</h3>";
                html << "<p><strong>Pavadinimas:</strong> " << pavadinimas << "</p>";
                html << "<p><strong>Kurejas:</strong> " << kurejas << "</p>";
                html << "<p><strong>Leidejas:</strong> " << leidejas << "</p>";
                html << "<p><strong>Isleidimo data:</strong> " << isleidimo_data << "</p>";
                html << "<p><strong>Zanras:</strong> " << zanras << "</p>";
            }

            html << "<a href='/'>Gristi i pradzia</a>";
            html << "</body></html>";

            return crow::response(html.str());
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
        catch (...) {
            return crow::response(500, "Įvyko nenumatyta klaida.");
        }
            });




    CROW_ROUTE(app, "/isiminti").methods("POST"_method)
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        if (aktyviosSesijos.count(session_id) == 0) {
            return crow::response(401, "Prisijunkite, kad galetumete isiminti skelbima.");
        }

        const auto& naudotojas = aktyviosSesijos[session_id];
        std::string role = naudotojas->gautiRole();
        if (role != "Registruotas" && role != "Administratorius") {
            return crow::response(403, "Neturite teises isiminti skelbimu.");
        }

        auto form_data = parseUrlEncoded(req.body);
        std::string skelbimo_id_str = form_data["SkelbimoID"];

        if (skelbimo_id_str.empty()) {
            return crow::response(400, "Truksta skelbimo ID.");
        }

        try {
            int skelbimo_id = std::stoi(skelbimo_id_str);
            std::string vardas = naudotojas->gautiVarda();

            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            // Gauti naudotojo ID pagal vardą
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
            stmt->setString(1, vardas);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            int naudotojo_id = -1;
            if (res->next()) {
                naudotojo_id = res->getInt("NaudotojasID");
            }
            else {
                return crow::response(500, "Nepavyko rasti naudotojo.");
            }

            std::unique_ptr<sql::PreparedStatement> creator_stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM skelbimas WHERE SkelbimoID=?"));
            creator_stmt->setInt(1, skelbimo_id);
            std::unique_ptr<sql::ResultSet> creator_res(creator_stmt->executeQuery());

            if (creator_res->next()) {
                int skelbejas_id = creator_res->getInt("NaudotojasID");
                if (skelbejas_id == naudotojo_id) {
                    return crow::response(403, "Negalite isiminti savo paties skelbimo.");
                }
            }
            else {
                return crow::response(404, "Skelbimas nerastas.");
            }


            // Patikrinti ar jau įsimintas
            std::unique_ptr<sql::PreparedStatement> check_stmt(
                conn->prepareStatement("SELECT COUNT(*) FROM isimintinas_skelbimas WHERE IsimintinoNaudotojoID=? AND SkelbimoID=?"));
            check_stmt->setInt(1, naudotojo_id);
            check_stmt->setInt(2, skelbimo_id);
            std::unique_ptr<sql::ResultSet> check_res(check_stmt->executeQuery());

            if (check_res->next() && check_res->getInt(1) > 0) {
                return crow::response(200, "Skelbimas jau isimintas.");
            }

            // Įrašyti įsimintą skelbimą
            std::unique_ptr<sql::PreparedStatement> insert_stmt(
                conn->prepareStatement("INSERT INTO isimintinas_skelbimas (IsimintinoNaudotojoID, SkelbimoID, Isiminimo_Data) VALUES (?, ?, NOW())"));
            insert_stmt->setInt(1, naudotojo_id);
            insert_stmt->setInt(2, skelbimo_id);
            insert_stmt->execute();

            return crow::response(200, "<html><head><meta charset='UTF-8'>"
"<script>"
"setTimeout(function() { window.location.href = '/skelbimas?id=" + std::to_string(skelbimo_id) + "'; }, 2000);"
"</script></head><body>"
"<div style='margin:20px; padding:10px; background:#d4edda; color:#155724; border:1px solid #c3e6cb; border-radius:5px;'>Skelbimas sekmingai isimintas!</div>"
"</body></html>");
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });
   
    CROW_ROUTE(app, "/panaikinti_is_isiminimo").methods("POST"_method)
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        if (aktyviosSesijos.count(session_id) == 0) {
            return crow::response(401, "Prisijunkite, kad galetumete panaikinti skelbima.");
        }

        const auto& naudotojas = aktyviosSesijos[session_id];
        std::string vardas = naudotojas->gautiVarda();

        auto form_data = parseUrlEncoded(req.body);
        std::string skelbimo_id_str = form_data["SkelbimoID"];

        if (skelbimo_id_str.empty()) {
            return crow::response(400, "Truksta skelbimo ID.");
        }

        try {
            int skelbimo_id = std::stoi(skelbimo_id_str);

            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            // Gauti naudotojo ID pagal vardą
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
            stmt->setString(1, vardas);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            int naudotojo_id = -1;
            if (res->next()) {
                naudotojo_id = res->getInt("NaudotojasID");
            }

            // Panaikinti įrašą iš isimintinas_skelbimas
            std::unique_ptr<sql::PreparedStatement> delete_stmt(
                conn->prepareStatement("DELETE FROM isimintinas_skelbimas WHERE IsimintinoNaudotojoID=? AND SkelbimoID=?"));
            delete_stmt->setInt(1, naudotojo_id);
            delete_stmt->setInt(2, skelbimo_id);
            delete_stmt->execute();

            return crow::response(200, "<html><head><meta charset='UTF-8'>"
"<script>"
"setTimeout(function() { window.location.href = '/skelbimas?id=" + std::to_string(skelbimo_id) + "'; }, 2000);"
"</script></head><body>"
"<div style='margin:20px; padding:10px; background:#d4edda; color:#155724; border:1px solid #c3e6cb; border-radius:5px;'>Sekmingai pasalinote skelbima is isiminamu!</div>"
"</body></html>");
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });

    CROW_ROUTE(app, "/mano_skelbimai")
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        if (aktyviosSesijos.count(session_id) == 0) {
            return crow::response(401, "Prisijunkite, kad galetumete matyti savo skelbimus.");
        }

        const auto& naudotojas = aktyviosSesijos[session_id];
        std::string vardas = naudotojas->gautiVarda();

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            // Gauti naudotojo ID
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
            stmt->setString(1, vardas);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            int naudotojo_id = -1;
            if (res->next()) {
                naudotojo_id = res->getInt("NaudotojasID");
            }
            else {
                return crow::response(500, "Nepavyko rasti naudotojo.");
            }

            // Gauti naudotojo skelbimus
            std::unique_ptr<sql::PreparedStatement> skelbimai_stmt(
                conn->prepareStatement("SELECT SkelbimoID FROM skelbimas WHERE NaudotojasID=?"));
            skelbimai_stmt->setInt(1, naudotojo_id);
            std::unique_ptr<sql::ResultSet> skelbimai_res(skelbimai_stmt->executeQuery());

            // Gauti žaidimų pavadinimus
            std::unique_ptr<sql::PreparedStatement> zaidimai_stmt(
                conn->prepareStatement("SELECT ZaidimoID, Pavadinimas FROM zaidimas"));
            std::unique_ptr<sql::ResultSet> zaidimai_res(zaidimai_stmt->executeQuery());

            std::ostringstream html;
            html << R"(<button onclick='window.history.back()' style = "margin-bottom: 10px;"> Atgal</button>)";
            html << "<h1>Mano skelbimai</h1>";
            if (!skelbimai_res->next()) {
                html << "<p style='color:gray;'>Siuo metu neturite sukurtu skelbimu.</p>";
            }
            else {
                do {
                    int id = skelbimai_res->getInt("SkelbimoID");
                    html << "<div style='border:1px solid #ccc; padding:10px; margin:10px;'>";
                    html << "<a href='/skelbimas?id=" << id << "'>Perziureti</a>";
                    html << "</div>";
                } while (skelbimai_res->next());
            }

            // Skelbimo sukūrimo forma
            html << "<hr><h2>Sukurti nauja skelbima</h2>";
            html << "<form action='/sukurti_skelbima' method='post' onsubmit='return patvirtintiSkelbima();'>";
            html << "<label>Žaidimas: </label><select name='zaidimo_id'>";
            while (zaidimai_res->next()) {
                int zaidimo_id = zaidimai_res->getInt("ZaidimoID");
                std::string pavad = static_cast<std::string>(zaidimai_res->getString("Pavadinimas"));
                html << "<option value='" << zaidimo_id << "'>" << pavad << "</option>";
            }
            html << "</select><br><br>";

            html << "<label>Kaina: </label><input type='number' step='0.01' name='kaina' required><br><br>";
            html << "<label>Kiekis: </label><input type='number' name='kiekis' required><br><br>";
            html << "<label>Aprasymas: </label><textarea name='aprasymas'></textarea><br><br>";
            html << "<button type='submit'>Sukurti skelbima</button>";
            html << "</form>";

            html << "<script>"
                << "function patvirtintiSkelbima() {"
                << "    return confirm('Ar tikrai norite sukurti si skelbima?');"
                << "}"
                << "</script>";

            return crow::response(html.str());

        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });

    CROW_ROUTE(app, "/sukurti_skelbima").methods("POST"_method)
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        if (aktyviosSesijos.count(session_id) == 0) {
            return crow::response(401, "Prisijunkite, kad galetumete sukurti skelbima.");
        }

        const auto& naudotojas = aktyviosSesijos[session_id];
        std::string vardas = naudotojas->gautiVarda();

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            // Gauti naudotojo ID
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
            stmt->setString(1, vardas);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            int naudotojo_id = -1;
            if (res->next()) {
                naudotojo_id = res->getInt("NaudotojasID");
            }
            else {
                return crow::response(500, "Nepavyko rasti naudotojo.");
            }


            std::map<std::string, std::string> form_data = parseUrlEncoded(req.body);


           

            int zaidimo_id = form_data.count("zaidimo_id") && !form_data["zaidimo_id"].empty() ? std::stoi(form_data["zaidimo_id"]) : -1;
            float kaina = form_data.count("kaina") && !form_data["kaina"].empty() ? std::stof(form_data["kaina"]) : 0.0f;
            std::string aprasymas = form_data["aprasymas"];
            int kiekis = form_data.count("kiekis") && !form_data["kiekis"].empty() ? std::stoi(form_data["kiekis"]) : 0;


            // Įrašyti skelbimą į duomenų bazę
            std::unique_ptr<sql::PreparedStatement> insert_stmt(
                conn->prepareStatement("INSERT INTO skelbimas (NaudotojasID, ZaidimoID, Kaina, Aprasymas, Zaidimo_Kiekis) VALUES (?, ?, ?, ?, ?)"));
            insert_stmt->setInt(1, naudotojo_id);
            insert_stmt->setInt(2, zaidimo_id);
            insert_stmt->setDouble(3, kaina);
            insert_stmt->setString(4, aprasymas);
            insert_stmt->setInt(5, kiekis);

            insert_stmt->execute();

            return crow::response(200, R"(
<html>
<head>
  <meta charset="UTF-8">
  <style>
    #message {
      background-color: #d4edda;
      color: #155724;
      padding: 15px;
      border: 1px solid #c3e6cb;
      border-radius: 5px;
      width: fit-content;
      margin: 20px auto;
      text-align: center;
      font-family: sans-serif;
    }
  </style>
  <script>
    setTimeout(function() {
      window.location.href = "/mano_skelbimai";
    }, 2000);
  </script>
</head>
<body>
  <div id="message">Skelbimas sekmingai sukurtas!</div>
</body>
</html>
)");

        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });

    CROW_ROUTE(app, "/redaguoti_skelbima").methods("POST"_method)
        ([](const crow::request& req) {
        auto data = parseUrlEncoded(req.body);
        int id = std::stoi(data["SkelbimoID"]);
        float kaina = std::stof(data["Kaina"]);
        int kiekis = std::stoi(data["Zaidimo_Kiekis"]);
        std::string aprasymas = data["Aprasymas"];

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("UPDATE skelbimas SET Kaina=?, Zaidimo_Kiekis=?, Aprasymas=? WHERE SkelbimoID=?"));
            stmt->setDouble(1, kaina);
            stmt->setInt(2, kiekis);
            stmt->setString(3, aprasymas);
            stmt->setInt(4, id);
            stmt->execute();

            crow::response res;
            res.code = 303;
            res.set_header("Location", "/skelbimas?id=" + std::to_string(id));
            return res;
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });

    CROW_ROUTE(app, "/istrinti_skelbima")
        ([](const crow::request& req) {
        std::string id_str = req.url_params.get("id") ? req.url_params.get("id") : "";
        if (id_str.empty()) return crow::response(400, "Truksta ID");

        int id = std::stoi(id_str);

        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("DELETE FROM skelbimas WHERE SkelbimoID=?"));
            stmt->setInt(1, id);
            stmt->execute();

            

            return crow::response(200, R"(
<html>
<head>
  <meta charset="UTF-8">
  <style>
    #message {
      background-color: #d4edda;
      color: #155724;
      padding: 15px;
      border: 1px solid #c3e6cb;
      border-radius: 5px;
      width: fit-content;
      margin: 20px auto;
      text-align: center;
      font-family: sans-serif;
    }
  </style>
  <script>
    setTimeout(function() {
      window.location.href = "/mano_skelbimai"; 
    }, 2000);
  </script>
</head>
<body>
  <div id="message">Skelbimas sekmingai istrintas!</div>
</body>
</html>
)");
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });

    CROW_ROUTE(app, "/isiminti_skelbimai")
        ([](const crow::request& req) {
        std::string session_id = req.get_header_value("Cookie");
        std::ostringstream html;
        html << R"(<button onclick='window.history.back()' style = "margin-bottom: 10px;"> Atgal</button>)";
        html << "<html><body><h1>Mano isiminti skelbimai</h1>";

        if (!aktyviosSesijos.count(session_id)) {
            html << "<p>Turite buti prisijunges, kad matytumete isimintus skelbimus.</p></body></html>";
            return crow::response(html.str());
        }

        try {
            auto& naudotojas = aktyviosSesijos[session_id];
            std::string vardas = naudotojas->gautiVarda();

            // Gauti naudotojo ID
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::SQLString url("jdbc:mariadb://127.0.0.1/komp_sis");
            sql::Properties props({ {"user", "root"}, {"password", "Asdfghjkl123"} });
            std::unique_ptr<sql::Connection> conn(driver->connect(url, props));

            std::unique_ptr<sql::PreparedStatement> user_stmt(
                conn->prepareStatement("SELECT NaudotojasID FROM naudotojas WHERE Prisijungimo_Vardas=?"));
            user_stmt->setString(1, vardas);
            std::unique_ptr<sql::ResultSet> user_res(user_stmt->executeQuery());

            int naudotojo_id = -1;
            if (user_res->next()) {
                naudotojo_id = user_res->getInt("NaudotojasID");
            }

            // Gauti įsimintus skelbimus
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "SELECT s.SkelbimoID, s.Aprasymas FROM isimintinas_skelbimas i "
                    "JOIN skelbimas s ON i.SkelbimoID = s.SkelbimoID "
                    "WHERE i.IsimintinoNaudotojoID=?"));
            stmt->setInt(1, naudotojo_id);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            
            if (!res->next()) {
                html << "<p style='color:gray;'>Siuo metu neturite isimintu skelbimu.</p>";
            }
            else {
                do {
                    int id = res->getInt("SkelbimoID");
                    std::string apr = std::string(res->getString("Aprasymas"));
                    html << "<div style='border:1px solid #ccc; padding:10px; margin:10px;'>";
                    html << "<a href='/skelbimas?id=" << id << "'>" << apr << "</a>";
                    html << "</div>";
                } while (res->next());
            }
            

            
            html << "</body></html>";
            return crow::response(html.str());
        }
        catch (const std::exception& e) {
            return crow::response(500, std::string("Klaida: ") + e.what());
        }
            });



    app.port(8080).multithreaded().run();
}
