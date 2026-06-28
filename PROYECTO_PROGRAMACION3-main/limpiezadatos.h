//
// Created by maydelithzuniga on 07/05/2026.
//

#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <future>
#include <thread>
using namespace std;

class limpiezadatos {
    string archivo_entrada="wiki_movie_plots_deduped.csv";
    string archivo_final="wiki_movie_plots_deduped_final.csv";
public:
    limpiezadatos()=default;
    ~limpiezadatos()=default;
    // función trim
    string trim(const string& s) {
        size_t inicio = s.find_first_not_of(" \t\r\n");
        if (inicio == string::npos) return ""; // solo espacios → vacío
        size_t fin = s.find_last_not_of(" \t\r\n");
        return s.substr(inicio, fin - inicio + 1);
    }
    string limpiar_espacio(const string& s) {
        string resultado = s;
        size_t pos = 0;
        while ((pos = resultado.find("\r\n", pos)) != string::npos) {
            resultado.replace(pos, 2, " ");
        }
        pos = 0;
        while ((pos = resultado.find("\r", pos)) != string::npos) {
            resultado.replace(pos, 1, " ");
        }
        return resultado;
    }

    inline string procesarFilaCadena(const string& linea) {
        vector<string> fila_procesada;
        string celda;
        bool en_comillas = false;

        for (char c : linea) {
            if (c == '"') {
                en_comillas = !en_comillas;
                celda += c;
            }
            else if (c == ',' && !en_comillas) {
                celda = trim(celda);
                if (celda == "" || celda == "Unknown" || celda == "\"\"" || celda == "\"Unknown\"") {
                    celda = "unknown";
                }
                celda = limpiar_espacio(celda);
                fila_procesada.push_back(celda);
                celda.clear();
            }
            else {
                celda += c;
            }
        }
        celda = trim(celda);
        if (celda == "" || celda == "Unknown" || celda == "\"\"" || celda == "\"Unknown\"") {
            celda = "unknown";
        }
        celda = limpiar_espacio(celda);
        fila_procesada.push_back(celda);

        // Reconstruimos la línea en formato CSV
        string resultado;
        for (size_t i = 0; i < fila_procesada.size(); i++) {
            resultado += fila_procesada[i];
            if (i != fila_procesada.size() - 1) {
                resultado += ",";
            }
        }
        resultado += "\n";
        return resultado;
    }

    void limpiardatoscsv() {
        auto inic = high_resolution_clock::now();

        ifstream entrada(archivo_entrada);
        if (!entrada.is_open()) {
            cout << "Error al abrir archivo de entrada" << endl;
            return;
        }

        string linea;
        vector<string> lineas_crudas;

        // ── FASE 1: Lectura secuencial ultra rápida al vector de memoria ─────────
        while (getline(entrada, linea)) {
            lineas_crudas.push_back(move(linea));
        }
        entrada.close();

        // ── FASE 2: Procesamiento y Limpieza de caracteres en PARALELO ───────────
        unsigned int numHilos = thread::hardware_concurrency();
        if (numHilos == 0) numHilos = 4; // Resguardo si no se detectan núcleos

        size_t totalLineas = lineas_crudas.size();
        size_t tamanoChunk = totalLineas / numHilos;

        // Vector global donde se guardarán las líneas finales ya limpias
        vector<string> lineas_limpias(totalLineas);
        vector<future<void>> futuros;

        for (unsigned int i = 0; i < numHilos; ++i) {
            size_t inicio = i * tamanoChunk;
            size_t fin = (i == numHilos - 1) ? totalLineas : inicio + tamanoChunk;

            // Lanzamos hilos de trabajo asíncronos
            futuros.push_back(async(launch::async, [this, inicio, fin, &lineas_crudas, &lineas_limpias]() {
                for (size_t j = inicio; j < fin; ++j) {
                    // Cada hilo escribe directamente en el índice asignado sin colisiones
                    lineas_limpias[j] = this->procesarFilaCadena(lineas_crudas[j]);
                }
            }));
        }

        // Esperamos a que todos los hilos terminen su procesamiento de texto
        for (auto& f : futuros) {
            f.get();
        }

        // ── FASE 3: Escritura secuencial ordenada en el archivo final ────────────
        ofstream salida(archivo_final);
        if (!salida.is_open()) {
            cout << "Error al abrir archivo de salida" << endl;
            return;
        }

        for (const auto& linea_lista : lineas_limpias) {
            salida << linea_lista;
        }
        salida.close();

        cout << "Archivo _copy (final) generado correctamente EN PARALELO." << endl;

        auto fin = high_resolution_clock::now();

        // Calcular duración en milisegundos
        auto duracion = duration_cast<milliseconds>(fin - inic);

        cout << "Tiempo: " << duracion.count() << " ms" << endl;

    }
};