#include "Simulador.h"
#include <fstream>
#include <algorithm>
#include <map>

void Simulador::exportarGanttSVG(const std::string& caminhoFicheiro) const {
    std::ofstream arq(caminhoFicheiro);
    if (!arq.is_open()) return;

    const auto& estadoFinal = historicoSnapshots_.back();
    long long tempoTotal = estadoFinal.relogioGlobal;

    // Ordena as tarefas por ID decrescente para o eixo Y (Requisito 2.5)
    std::vector<Tarefa> tarefasOrdenadas = estadoFinal.tarefas;
    std::sort(tarefasOrdenadas.begin(), tarefasOrdenadas.end(),
        [](const Tarefa& a, const Tarefa& b) { return a.id > b.id; });

    int larguraBloco = 30;
    int alturaLinha = 40;
    int margemEsquerda = 100;
    int margemTopo = 60;

    int larguraTotal = margemEsquerda + static_cast<int>(tempoTotal + 2) * larguraBloco;
    int alturaTotal = margemTopo + static_cast<int>(tarefasOrdenadas.size()) * alturaLinha + 60;

    // Cabeçalho SVG
    arq << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << larguraTotal
        << "\" height=\"" << alturaTotal << "\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";

    // Título
    arq << "<text x=\"20\" y=\"30\" font-family=\"Arial\" font-size=\"18\" font-weight=\"bold\">"
        << "Gráfico de Gantt - Algoritmo: " << configOriginal_.algoritmo << "</text>\n";

    // Eixo de Tempo (X)
    for (long long t = 0; t <= tempoTotal; ++t) {
        int x = margemEsquerda + static_cast<int>(t) * larguraBloco;
        arq << "<line x1=\"" << x << "\" y1=\"" << margemTopo
            << "\" x2=\"" << x << "\" y2=\"" << (alturaTotal - 40)
            << "\" stroke=\"#e0e0e0\" stroke-dasharray=\"2,2\"/>\n"
            << "<text x=\"" << x << "\" y=\"" << (alturaTotal - 20)
            << "\" font-family=\"Arial\" font-size=\"10\" text-anchor=\"middle\">" << t << "</text>\n";
    }

    // Desenha as Tarefas e as suas Execuções (Eixo Y)
    for (size_t idx = 0; idx < tarefasOrdenadas.size(); ++idx) {
        const auto& t = tarefasOrdenadas[idx];
        int y = margemTopo + static_cast<int>(idx) * alturaLinha;

        // Rótulo da Tarefa
        arq << "<text x=\"10\" y=\"" << (y + 25)
            << "\" font-family=\"Arial\" font-size=\"12\" font-weight=\"bold\">"
            << "Tarefa " << t.id << "</text>\n";

        // Linha guia horizontal
        arq << "<line x1=\"" << margemEsquerda << "\" y1=\"" << (y + 20)
            << "\" x2=\"" << (larguraTotal - 20) << "\" y2=\"" << (y + 20)
            << "\" stroke=\"#cccccc\" stroke-width=\"1\"/>\n";
    }

    // Mapeamento de instantes de execução a partir dos snapshots
    for (const auto& snap : historicoSnapshots_) {
        long long t = snap.relogioGlobal;
        if (t == 0) continue;

        for (const auto& reg : snap.registosTick) {
            // Encontra a posição Y da tarefa
            int idxY = -1;
            for (size_t i = 0; i < tarefasOrdenadas.size(); ++i) {
                if (tarefasOrdenadas[i].id == reg.idTarefa) {
                    idxY = static_cast<int>(i);
                    break;
                }
            }
            if (idxY == -1) continue;

            int x = margemEsquerda + static_cast<int>(t - 1) * larguraBloco;
            int y = margemTopo + idxY * alturaLinha + 5;

            if (reg.estado == EstadoOcorrencia::Executando) {
                // Procura a cor da tarefa
                std::string corHex = "000000";
                for (const auto& tar : estadoFinal.tarefas) {
                    if (tar.id == reg.idTarefa) { corHex = tar.cor; break; }
                }

                // Bloco de Execução colorido (Requisito 2.1)
                arq << "<rect x=\"" << x << "\" y=\"" << y
                    << "\" width=\"" << larguraBloco << "\" height=\"30\" fill=\"#" << corHex
                    << "\" stroke=\"#000000\" stroke-width=\"0.5\"/>\n"
                    << "<text x=\"" << (x + larguraBloco / 2) << "\" y=\"" << (y + 18)
                    << "\" font-family=\"Arial\" font-size=\"9\" fill=\"#ffffff\" font-weight=\"bold\" text-anchor=\"middle\">"
                    << "CPU" << reg.idCPU << "</text>\n";
            } else if (reg.estado == EstadoOcorrencia::Pronta) {
                // Moldura sem preenchimento para Fila de Espera (Requisito 2.1)
                arq << "<rect x=\"" << x << "\" y=\"" << y
                    << "\" width=\"" << larguraBloco << "\" height=\"30\" fill=\"none\" stroke=\"#888888\" stroke-dasharray=\"2,2\"/>\n";
            }

            // Marcador de Conclusão / Perda de Prazo (Requisito 2.2)
            if (reg.eventoPerdaPrazo) {
                arq << "<text x=\"" << (x + larguraBloco) << "\" y=\"" << (y + 12)
                    << "\" font-family=\"Arial\" font-size=\"14\" fill=\"red\" font-weight=\"bold\">✖</text>\n";
            }

            if (reg.eventoFimQuantum) {
                arq << "<line x1=\"" << (x + larguraBloco)
                    << "\" y1=\"" << y
                    << "\" x2=\"" << (x + larguraBloco)
                    << "\" y2=\"" << (y + 30)
                    << "\" stroke=\"#000000\" stroke-width=\"2\"/>\n";
            }
        }
    }

    arq << "</svg>\n";
}