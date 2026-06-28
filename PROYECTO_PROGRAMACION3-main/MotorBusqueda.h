#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <future>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;
// ── Índices de columnas del CSV de Wikipedia ──────────────────────────────────
static constexpr int COL_ANIO      = 0;
static constexpr int COL_TITULO    = 1;
static constexpr int COL_DIRECTOR  = 3;
static constexpr int COL_CAST      = 4;
static constexpr int COL_GENERO    = 5;
static constexpr int COL_SINOPSIS  = 7;


// ── Estructura de película ────────────────────────────────────────────────────
struct Pelicula {
    int id;
    string anio;
    string titulo;
    string sinopsis;
    vector<string> directores;
    vector<string> actores;
    vector<string> generos;
};

// ── Parsear una línea CSV respetando comas dentro de comillas ─────────────────
inline std::vector<std::string> parsearLinea(const std::string& linea) {
    std::vector<std::string> fila;
    std::string celda;
    bool en_comillas = false;

    for (size_t i = 0; i < linea.size(); i++) {
        char c = linea[i];

        if (c == '"') {
            // Comilla doble escapada ("") dentro de campo
            if (en_comillas && i + 1 < linea.size() && linea[i + 1] == '"') {
                celda += '"';
                i++; // saltar la segunda comilla
            } else {
                en_comillas = !en_comillas;
                // NO agregamos la comilla a la celda
            }
        } else if (c == ',' && !en_comillas) {
            fila.push_back(celda);
            celda.clear();
        } else {
            celda += c;
        }
    }
    fila.push_back(celda); // última celda
    return fila;
}

inline vector<string> dividir( string& texto) {
    vector<string> fila;
    string celda;
    if (texto.size() >= 2 && texto.front() == '"' && texto.back() == '"')
        texto = texto.substr(1, texto.size() - 2);

    for (char c : texto) {
        if (c == ',') {
            // trim antes de guardar
            size_t ini = celda.find_first_not_of(" \t");
            size_t fin = celda.find_last_not_of(" \t");
            if (ini != string::npos)
                fila.push_back(celda.substr(ini, fin - ini + 1));
            else
                fila.push_back("unknown"); // celda solo espacios
            celda.clear();
        } else {
            celda += c;
        }
    }
    // última celda
    size_t ini = celda.find_first_not_of(" \t");
    size_t fin = celda.find_last_not_of(" \t");
    if (ini != string::npos)
        fila.push_back(celda.substr(ini, fin - ini + 1));
    return fila;
}

// ── Limpieza: minúsculas + eliminar puntuación especial ──────────────────────
inline std::string limpiar(std::string texto) {
    // Minúsculas
    std::transform(texto.begin(), texto.end(), texto.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Eliminar: . , ( ) ! ? ; : " ' - _ / \ | [ ] { } * # @ ^ ~ ` + = < > %
    const std::string descartados = ".,()!?;:\"'\\-_/|[]{}*#@^~`+=<>%";
    std::string resultado;
    resultado.reserve(texto.size());
    for (char c : texto) {
        if (descartados.find(c) == std::string::npos)
            resultado += c;
    }
    return resultado;
}

// ── Tokenización: divide string limpio en palabras ───────────────────────────
inline std::vector<std::string> tokenizar(std::string texto) {
    texto = limpiar(texto);
    std::vector<std::string> tokens;
    std::istringstream ss(texto);
    std::string palabra;
    while (ss >> palabra) {
        if (!palabra.empty())
            tokens.push_back(palabra);
    }
    return tokens;
}

// detectar si una línea empieza con un año (4 dígitos seguidos de coma)
inline bool esInicioNuevaPelicula(const string& linea) {
    if (linea.size() < 5) return false;
    return isdigit(linea[0]) &&
           isdigit(linea[1]) &&
           isdigit(linea[2]) &&
           isdigit(linea[3]) &&
           linea[4] == ',';
}

// ── Carga el CSV y devuelve vector de Pelicula ────────────────────────────────
inline unordered_map<int, Pelicula> cargarCSV(const string& ruta = "wiki_movie_plots_deduped_final.csv") {
    auto inic = high_resolution_clock::now();

    unordered_map<int, Pelicula> catalogo;
    ifstream archivo(ruta);

    if (!archivo.is_open()) {
        cerr << "[MotorBusqueda] Error: no se pudo abrir \"" << ruta << "\"\n";
        return catalogo;
    }

    string linea;
    bool primeraLinea = true;
    vector<string> lineasCompletas;

    while (getline(archivo, linea)) {
        if (primeraLinea) { primeraLinea = false; continue; }
        if (linea.empty()) continue;

        string lineaCompleta = linea;
        while (true) {
            streampos posicion = archivo.tellg();
            string siguiente;
            if (!getline(archivo, siguiente)) break;

            if (esInicioNuevaPelicula(siguiente)) {
                archivo.seekg(posicion);
                break;
            }
            lineaCompleta += "\n" + siguiente;
        }
        lineasCompletas.push_back(move(lineaCompleta));
    }
    archivo.close();

    unsigned int numHilos = thread::hardware_concurrency();
    if (numHilos == 0) numHilos = 4; // Por seguridad si no detecta ninguno

    size_t totalPeliculas = lineasCompletas.size();
    size_t tamanoChunk = totalPeliculas / numHilos;

    // Aquí guardaremos las promesas/futuros de cada hilo
    vector<future<vector<Pelicula>>> futuros;

    for (unsigned int i = 0; i < numHilos; ++i) {
        size_t inicio = i * tamanoChunk;
        // El último hilo se asegura de llevarse el residuo de la división
        size_t fin = (i == numHilos - 1) ? totalPeliculas : inicio + tamanoChunk;

        // Lanzamos la tarea asíncrona en un hilo separado
        futuros.push_back(async(launch::async, [inicio, fin, &lineasCompletas]() {
            vector<Pelicula> peliculasLocales;
            peliculasLocales.reserve(fin - inicio);

            for (size_t j = inicio; j < fin; ++j) {
                // Cada hilo parsea su fila de forma independiente
                vector<string> fila = parsearLinea(lineasCompletas[j]);

                Pelicula p;
                // Usamos 'j' como ID único secuencial (reemplaza a catalogo.size())
                p.id       = static_cast<int>(j);
                p.anio     = fila.size() > COL_ANIO      ? fila[COL_ANIO]      : "unknown";
                p.titulo   = fila.size() > COL_TITULO    ? fila[COL_TITULO]    : "unknown";
                p.sinopsis = fila.size() > COL_SINOPSIS  ? fila[COL_SINOPSIS]  : "unknown";

                string director = fila.size() > COL_DIRECTOR ? fila[COL_DIRECTOR] : "unknown";
                string cast     = fila.size() > COL_CAST     ? fila[COL_CAST]     : "unknown";
                string genero   = fila.size() > COL_GENERO   ? fila[COL_GENERO]   : "unknown";

                // Modificar variables locales es 100% seguro entre hilos
                p.directores = director != "unknown" ? dividir(director) : vector<string>{"unknown"};
                p.actores    = cast     != "unknown" ? dividir(cast)     : vector<string>{"unknown"};
                p.generos    = genero   != "unknown" ? dividir(genero)   : vector<string>{"unknown"};

                peliculasLocales.push_back(move(p));
            }
            return peliculasLocales;
        }));
    }

    for (auto& f : futuros) {
        // f.get() bloquea temporalmente hasta que el hilo en cuestión termine y entrega su vector
        vector<Pelicula> subLista = f.get();
        for (auto& p : subLista) {
            catalogo[p.id] = move(p);
        }
    }

    cout << "[MotorBusqueda] " << catalogo.size()
         << " peliculas cargadas EN PARALELO desde \"" << ruta << "\"\n";

    auto fin = high_resolution_clock::now();

    // Calcular duración en milisegundos
    auto duracion = duration_cast<milliseconds>(fin - inic);

    cout << "Tiempo: " << duracion.count() << " ms" << endl;

    return catalogo;
}

// ── Búsqueda por token en título y sinopsis ───────────────────────────────────
inline vector<Pelicula> buscarPorPalabra(
    const unordered_map<int, Pelicula>& catalogo,
    const string& consulta
) {
    auto inic = high_resolution_clock::now();

    vector<string> tokensConsulta = tokenizar(consulta);
    vector<const Pelicula*> todasLasPeliculas;

    for (const auto& par : catalogo)
        todasLasPeliculas.push_back(&par.second);

    if (todasLasPeliculas.empty())
        return {};

    unsigned int numHilos = std::thread::hardware_concurrency();
    if (numHilos == 0)
        numHilos = 4;

    numHilos = min<unsigned int>(numHilos, todasLasPeliculas.size());

    size_t tamanoChunk = todasLasPeliculas.size() / numHilos;
    size_t resto = todasLasPeliculas.size() % numHilos;

    vector<future<vector<Pelicula>>> futuros;

    size_t inicio = 0;

    for (unsigned int i = 0; i < numHilos; ++i) {
        size_t fin = inicio + tamanoChunk + (i < resto ? 1 : 0);

        futuros.push_back(std::async(std::launch::async,
            [inicio, fin, &todasLasPeliculas, &tokensConsulta]() {

                vector<Pelicula> resultadosLocales;

                for (size_t j = inicio; j < fin; ++j) {
                    const Pelicula& p = *todasLasPeliculas[j];
                    bool encontrado = false;

                    // Buscar en el título
                    vector<string> tokensTitulo = tokenizar(p.titulo);

                    for (const auto& palabra : tokensTitulo) {
                        for (const auto& token : tokensConsulta) {
                            if (palabra == token) {
                                resultadosLocales.push_back(p);
                                encontrado = true;
                                break;
                            }
                        }
                        if (encontrado)
                            break;
                    }

                    if (encontrado)
                        continue;

                    // Buscar en la sinopsis
                    vector<string> tokensSinopsis = tokenizar(p.sinopsis);

                    for (const auto& palabra : tokensSinopsis) {
                        for (const auto& token : tokensConsulta) {
                            if (palabra == token) {
                                resultadosLocales.push_back(p);
                                encontrado = true;
                                break;
                            }
                        }
                        if (encontrado)
                            break;
                    }
                }

                return resultadosLocales;
            }));

        inicio = fin;
    }

    vector<Pelicula> resultados;

    for (auto& futuro : futuros) {
        vector<Pelicula> parciales = futuro.get();
        resultados.insert(resultados.end(), parciales.begin(), parciales.end());
    }

    auto fin = high_resolution_clock::now();

    // Calcular duración en milisegundos
    auto duracion = duration_cast<milliseconds>(fin - inic);

    cout << "Tiempo: " << duracion.count() << " ms" << endl;
    return resultados;
}
// ── Búsqueda por tag: director, casting o género ──────────────────────────────
inline vector<Pelicula> buscarPorTag(
    const unordered_map<int, Pelicula>& catalogo,
    const string& consulta
) {
    vector<string> tokensConsulta = tokenizar(consulta);
    vector<Pelicula> resultados;
    for (const auto& par : catalogo)
    {
        const Pelicula& p = par.second;
        bool encontrado = false;
        // Buscar en directores
        for (const auto& director : p.directores) {
            vector<string> tokensDirector = tokenizar(director);
            for (const auto& palabra : tokensDirector) {
                for (const auto& token : tokensConsulta) {
                    if (palabra==token) {
                        resultados.push_back(p);
                        encontrado = true;
                        break;
                    }
                }
                if (encontrado) {
                    break;
                }
            }
            if (encontrado) {
                break;
            }
        }
        if (encontrado) {
            continue;
        }
        // Buscar en actores / casting
        for (const auto& actor : p.actores) {
            vector<string> tokensActor = tokenizar(actor);
            for (const auto& palabra : tokensActor) {
                for (const auto& token : tokensConsulta) {
                    if (palabra==token) {
                        resultados.push_back(p);
                        encontrado = true;
                        break;
                    }
                }
                if (encontrado) {
                    break;
                }
            }
            if (encontrado) {
                break;
            }
        }
        if (encontrado) {
            continue;
        }
        // Buscar en géneros
        for (const auto& genero : p.generos) {
            vector<string> tokensGenero = tokenizar(genero);
            for (const auto& palabra : tokensGenero) {
                for (const auto& token : tokensConsulta) {
                    if (palabra==token) {
                        resultados.push_back(p);
                        encontrado = true;
                        break;
                    }
                }
                if (encontrado) {
                    break;
                }
            }
            if (encontrado) {
                break;
            }
        }
    }
    return resultados;
}
inline vector<Pelicula> top5(const vector<Pelicula>& resultados,
                              const string& consulta) {
    vector<string> tokens = tokenizar(consulta);

    vector<pair<int, Pelicula>> conteo;

    for (const auto& p : resultados) {
        int score = 0;

        // título — peso 100
        for (const auto& t : tokenizar(p.titulo))
            for (const auto& token : tokens)
                if (t.find(token) != string::npos) score += 100;

        // Géneros — peso 50
        for (const auto& genero : p.generos) {
            for (const auto& palabra : tokenizar(genero)) {
                for (const auto& token : tokens) {
                    if (palabra.find(token) != string::npos) {
                        score += 50;
                    }
                }
            }
        }

        // directores — peso 10
        for (const auto& d : p.directores)
            for (const auto& t : tokenizar(d))
                for (const auto& token : tokens)
                    if (t.find(token) != string::npos) score += 10;

        // actores — peso 10
        for (const auto& a : p.actores)
            for (const auto& t : tokenizar(a))
                for (const auto& token : tokens)
                    if (t.find(token) != string::npos) score += 10;

        // sinopsis — peso 1
        for (const auto& t : tokenizar(p.sinopsis))
            for (const auto& token : tokens)
                if (t.find(token) != string::npos) score += 1;

        conteo.push_back({score, p});
    }

    sort(conteo.begin(), conteo.end(),
         [](const pair<int, Pelicula>& a, const pair<int, Pelicula>& b) {
             return a.first > b.first;
         });

    vector<Pelicula> resultado;
    int limite = min(5, (int)conteo.size());
    for (int i = 0; i < limite; i++)
        resultado.push_back(conteo[i].second);

    return resultado;
}