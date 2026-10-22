#include "Simulador.h"
#include <fstream>
#include <algorithm>
#include <map>
#include <stdexcept>

// Exporta para SVG o estado da simulação até o passo atual.
// Os snapshots fornecem os eventos e estados; este método os converte
// em formas e textos, sem alterar a simulação.
void Simulador::exportarGanttSVG(const std::string& caminhoFicheiro) const {
    std::ofstream arq(caminhoFicheiro);
    if (!arq.is_open()) {
        throw std::runtime_error(
        "Nao foi possivel criar o arquivo SVG: " + caminhoFicheiro);
    }

    const auto& estadoFinal = getEstadoAtual();
    long long tempoTotal = estadoFinal.relogioGlobal;

    // Ordena as tarefas por ID decrescente para o eixo Y (Requisito 2.5)
    std::vector<Tarefa> tarefasOrdenadas = estadoFinal.tarefas;
    std::sort(tarefasOrdenadas.begin(), tarefasOrdenadas.end(),
        [](const Tarefa& a, const Tarefa& b) { return a.id > b.id; });

    // Define a escala e as margens do desenho. Cada tick ocupa a mesma largura,
    // para que os intervalos de execução possam ser comparados visualmente.
    int larguraBloco = 30;
    int alturaLinha = 40;
    int margemEsquerda = 100;
    int margemTopo = 60;

    int larguraTotal = std::max(
        800,
        margemEsquerda + static_cast<int>(tempoTotal + 2) * larguraBloco);

    int yInicioCPUs =
        margemTopo + static_cast<int>(tarefasOrdenadas.size()) * alturaLinha;

    int alturaLinhaCPU = 28;

    int fimLinhas =
        yInicioCPUs +
        static_cast<int>(estadoFinal.cpus.size()) * alturaLinhaCPU;

    int alturaTotal = fimLinhas + 115;

    // Abre o documento SVG e define padrões reutilizáveis para ocorrências
    // suspensas e períodos em que uma CPU ficou desligada.
    arq << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << larguraTotal
        << "\" height=\"" << alturaTotal << "\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";

        arq << "<defs>\n"
            << "<pattern id=\"suspenso\" "
            << "patternUnits=\"userSpaceOnUse\" width=\"6\" height=\"6\">\n"
            << "<rect width=\"6\" height=\"6\" fill=\"#000000\"/>\n"
            << "<circle cx=\"3\" cy=\"3\" r=\"1\" fill=\"#ffffff\"/>\n"
            << "</pattern>\n"
            << "<pattern id=\"cpuDesligada\" "
            << "patternUnits=\"userSpaceOnUse\" width=\"6\" height=\"6\">\n"
            << "<rect width=\"6\" height=\"6\" fill=\"#eeeeee\"/>\n"
            << "<path d=\"M0 6 L6 0\" "
            << "stroke=\"#555555\" stroke-width=\"1\"/>\n"
            << "</pattern>\n"
            << "</defs>\n";

    // Título
    arq << "<text x=\"20\" y=\"30\" font-family=\"Arial\" font-size=\"18\" "
        << "font-weight=\"bold\">"
        << "Gráfico de Gantt - Algoritmo: " << configOriginal_.algoritmo
        << "</text>\n";

    // Eixo de Tempo (X)
    for (long long t = 0; t <= tempoTotal; ++t) {
        int x = margemEsquerda + static_cast<int>(t) * larguraBloco;
        arq << "<line x1=\"" << x << "\" y1=\"" << margemTopo
            << "\" x2=\"" << x << "\" y2=\"" << fimLinhas
            << "\" stroke=\"#e0e0e0\" stroke-dasharray=\"2,2\"/>\n"
            << "<text x=\"" << x << "\" y=\"" << (fimLinhas + 15)
            << "\" font-family=\"Arial\" font-size=\"10\" text-anchor=\"middle\">"
            << t << "</text>\n";
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

    // Desenha uma linha e um rótulo para cada CPU, abaixo das linhas das tarefas.
    for (size_t c = 0; c < estadoFinal.cpus.size(); ++c) {
        int y = yInicioCPUs + static_cast<int>(c) * alturaLinhaCPU;

        arq << "<text x=\"10\" y=\"" << (y + 18)
            << "\" font-family=\"Arial\" font-size=\"12\" "
            << "font-weight=\"bold\">CPU " << c << "</text>\n"
            << "<line x1=\"" << margemEsquerda << "\" y1=\"" << (y + 14)
            << "\" x2=\"" << (larguraTotal - 20) << "\" y2=\"" << (y + 14)
            << "\" stroke=\"#cccccc\" stroke-width=\"1\"/>\n";
    }

    // Percorre os registros dos snapshots executados até o passo atual.
    // Cada registro vira um bloco, marcador ou linha na posição da tarefa e do tick.
    for (size_t passo = 1; passo <= indicePassoAtual_; ++passo) {
        const auto& snap = historicoSnapshots_[passo];
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
            } 
            else if (reg.estado == EstadoOcorrencia::Pronta) {
                // Moldura sem preenchimento para Fila de Espera (Requisito 2.1)
                arq << "<rect x=\"" << x << "\" y=\"" << y
                    << "\" width=\"" << larguraBloco << "\" height=\"30\" fill=\"none\" stroke=\"#888888\" stroke-dasharray=\"2,2\"/>\n";
            }
            else if (reg.estado == EstadoOcorrencia::Suspensa) {
                arq << "<rect x=\"" << x << "\" y=\"" << y
                    << "\" width=\"" << larguraBloco
                    << "\" height=\"30\" fill=\"url(#suspenso)\" "
                    << "stroke=\"#000000\"/>\n";
            }

            // Marcador de chegada
            if (reg.eventoChegada) {
                arq << "<circle cx=\"" << (x + 6) << "\" cy=\"" << (y + 6)
                    << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#008000\" "
                    << "stroke-width=\"2\"/>\n"
                    << "<text x=\"" << (x + 6) << "\" y=\"" << (y + 9)
                    << "\" font-family=\"Arial\" font-size=\"7\" "
                    << "font-weight=\"bold\" text-anchor=\"middle\" "
                    << "fill=\"#008000\">A</text>\n";
            }

            // Marcador de conclusão
            if (reg.eventoConclusao) {
                arq << "<circle cx=\"" << (x + larguraBloco - 6)
                    << "\" cy=\"" << (y + 6)
                    << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#000000\" "
                    << "stroke-width=\"2\"/>\n"
                    << "<text x=\"" << (x + larguraBloco - 6)
                    << "\" y=\"" << (y + 9)
                    << "\" font-family=\"Arial\" font-size=\"7\" "
                    << "font-weight=\"bold\" text-anchor=\"middle\" "
                    << "fill=\"#000000\">F</text>\n";
            }

            // Marcador de perda de prazo
            if (reg.eventoPerdaPrazo) {
                arq << "<circle cx=\"" << (x + larguraBloco / 2)
                    << "\" cy=\"" << (y + 6)
                    << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#ff0000\" "
                    << "stroke-width=\"2\"/>\n"
                    << "<text x=\"" << (x + larguraBloco / 2)
                    << "\" y=\"" << (y + 9)
                    << "\" font-family=\"Arial\" font-size=\"8\" "
                    << "font-weight=\"bold\" text-anchor=\"middle\" "
                    << "fill=\"#ff0000\">!</text>\n";
            }

            // Marcador de escolha por sorteio
            if (reg.eventoSorteio) {
                arq << "<polygon points=\""
                    << (x + larguraBloco / 2) << "," << (y + 21) << " "
                    << (x + larguraBloco / 2 + 5) << "," << (y + 26) << " "
                    << (x + larguraBloco / 2) << "," << (y + 31) << " "
                    << (x + larguraBloco / 2 - 5) << "," << (y + 26)
                    << "\" fill=\"#0000ff\" stroke=\"#ffffff\" "
                    << "stroke-width=\"1\">\n"
                    << "<title>Escolha por sorteio</title>\n"
                    << "</polygon>\n";
            }

            // Linha indicando fim do quantum
            if (reg.eventoFimQuantum) {
                arq << "<line x1=\"" << (x + larguraBloco)
                    << "\" y1=\"" << y
                    << "\" x2=\"" << (x + larguraBloco)
                    << "\" y2=\"" << (y + 30)
                    << "\" stroke=\"#000000\" stroke-width=\"2\"/>\n";
            }
        }
    }

    for (size_t passo = 1; passo <= indicePassoAtual_; ++passo) {
        const auto& snapshot = historicoSnapshots_[passo];
        
        // Desenha uma faixa hachurada por CPU e tick ocioso.
        // O snapshot inicial é ignorado porque ainda não representa um tick executado.
        for (size_t c = 0; c < snapshot.cpusDesligadasNoTick.size(); ++c) {
            if (!snapshot.cpusDesligadasNoTick[c]) {
                continue;
            }

            int x = margemEsquerda +
                static_cast<int>(snapshot.relogioGlobal - 1) * larguraBloco;
            int y = yInicioCPUs +
                static_cast<int>(c) * alturaLinhaCPU + 4;

            arq << "<rect x=\"" << x << "\" y=\"" << y
                << "\" width=\"" << larguraBloco
                << "\" height=\"20\" fill=\"url(#cpuDesligada)\" "
                << "stroke=\"#333333\" stroke-width=\"0.5\">\n"
                << "<title>CPU " << c << " desligada no tick "
                << (snapshot.relogioGlobal - 1) << "</title>\n"
                << "</rect>\n";
        }
    }

    int legendaY = fimLinhas + 42;
    int coluna1 = margemEsquerda;
    int coluna2 = margemEsquerda + 175;
    int coluna3 = margemEsquerda + 350;
    int coluna4 = margemEsquerda + 525;

    arq << "<text x=\"" << margemEsquerda << "\" y=\"" << (legendaY - 12)
        << "\" font-family=\"Arial\" font-size=\"12\" "
        << "font-weight=\"bold\">Legenda</text>\n";

    // Linha 1: estados
    arq << "<rect x=\"" << coluna1 << "\" y=\"" << legendaY - 10
        << "\" width=\"18\" height=\"14\" fill=\"#808080\"/>\n"
        << "<text x=\"" << coluna1 + 24 << "\" y=\"" << legendaY + 2
        << "\" font-family=\"Arial\" font-size=\"11\">Executando</text>\n";

    arq << "<rect x=\"" << coluna2 << "\" y=\"" << legendaY - 10
        << "\" width=\"18\" height=\"14\" fill=\"none\" "
        << "stroke=\"#888888\" stroke-dasharray=\"2,2\"/>\n"
        << "<text x=\"" << coluna2 + 24 << "\" y=\"" << legendaY + 2
        << "\" font-family=\"Arial\" font-size=\"11\">Pronta</text>\n";

    arq << "<rect x=\"" << coluna3 << "\" y=\"" << legendaY - 10
        << "\" width=\"18\" height=\"14\" fill=\"url(#suspenso)\" "
        << "stroke=\"#000000\"/>\n"
        << "<text x=\"" << coluna3 + 24 << "\" y=\"" << legendaY + 2
        << "\" font-family=\"Arial\" font-size=\"11\">Suspensa</text>\n";

    // Linha 2: eventos
    int eventosY = legendaY + 25;

    arq << "<circle cx=\"" << coluna1 + 7 << "\" cy=\"" << eventosY - 4
        << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#008000\" stroke-width=\"2\"/>"
        << "<text x=\"" << coluna1 + 18 << "\" y=\"" << eventosY
        << "\" font-family=\"Arial\" font-size=\"11\">Chegada</text>\n";

    arq << "<circle cx=\"" << coluna2 + 7 << "\" cy=\"" << eventosY - 4
        << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#000000\" stroke-width=\"2\"/>"
        << "<text x=\"" << coluna2 + 18 << "\" y=\"" << eventosY
        << "\" font-family=\"Arial\" font-size=\"11\">Conclusão</text>\n";

    arq << "<circle cx=\"" << coluna3 + 7 << "\" cy=\"" << eventosY - 4
        << "\" r=\"5\" fill=\"#ffffff\" stroke=\"#ff0000\" stroke-width=\"2\"/>"
        << "<text x=\"" << coluna3 + 18 << "\" y=\"" << eventosY
        << "\" font-family=\"Arial\" font-size=\"11\">Prazo perdido</text>\n";

    arq << "<polygon points=\""
        << coluna4 + 2 << "," << eventosY - 4 << " "
        << coluna4 + 7 << "," << eventosY - 9 << " "
        << coluna4 + 12 << "," << eventosY - 4 << " "
        << coluna4 + 7 << "," << eventosY + 1
        << "\" fill=\"#0000ff\"/>\n"
        << "<text x=\"" << coluna4 + 18 << "\" y=\"" << eventosY
        << "\" font-family=\"Arial\" font-size=\"11\">Sorteio</text>\n";

    arq << "<line x1=\"" << coluna1 << "\" y1=\"" << eventosY + 18
        << "\" x2=\"" << coluna1 + 18 << "\" y2=\"" << eventosY + 18
        << "\" stroke=\"#000000\" stroke-width=\"2\"/>\n"
        << "<text x=\"" << coluna1 + 24 << "\" y=\"" << eventosY + 22
        << "\" font-family=\"Arial\" font-size=\"11\">Fim do quantum</text>\n";
    
    // Terceira linha da legenda: explica a hachura usada nos períodos de CPU desligada.
    int xLegendaCPU = margemEsquerda + 175;
    int yLegendaCPU = alturaTotal - 26;

    arq << "<rect x=\"" << xLegendaCPU
        << "\" y=\"" << yLegendaCPU - 13
        << "\" width=\"18\" height=\"14\" "
        << "fill=\"url(#cpuDesligada)\" "
        << "stroke=\"#333333\" stroke-width=\"1\"/>\n"
        << "<text x=\"" << xLegendaCPU + 24
        << "\" y=\"" << yLegendaCPU
        << "\" font-family=\"Arial\" font-size=\"11\">"
        << "CPU desligada</text>\n";
        
    arq << "</svg>\n";
    arq.close();

    if (!arq) {
        throw std::runtime_error(
            "Ocorreu um erro ao gravar o arquivo SVG: " + caminhoFicheiro);
    }
}