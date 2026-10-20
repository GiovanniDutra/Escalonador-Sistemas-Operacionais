#include <iostream>
#include <string>
#include "LeitorConfig.h"
#include "Simulador.h"

void exibirAjuda() {
    std::cout << "\nComandos do Modo Passo-a-Passo:\n"
        << "  [ENTER] : Avanca 1 tick\n"
        << "  b        : Retrocede 1 tick\n"
        << "  g        : Exporta o Grafico de Gantt SVG atual\n"
        << "  q        : Sair\n";
}

int main(int argc, char* argv[]) {
    std::string caminhoFicheiro;

    // 1. Tenta pegar do argumento de linha de comando (ex: ./simulador E:/config.txt)
    if (argc > 1) {
        caminhoFicheiro = argv[1];
    }
    else {
        // 2. Se nenhum argumento foi passado, solicita ao usuário interativamente
        std::cout << "Digite o caminho do ficheiro de configuracao (ex: E:\\config.txt ou /media/pendrive/config.txt)\n";
        std::cout << "Ou pressione [ENTER] para usar 'config.txt' da pasta atual: ";
        std::getline(std::cin, caminhoFicheiro);

        if (caminhoFicheiro.empty()) {
            caminhoFicheiro = "config.txt";
        }
    }

    try {
        std::cout << "\nA carregar ficheiro de configuracao: " << caminhoFicheiro << "...\n";
        ConfiguracaoSistema config = carregarConfiguracao(caminhoFicheiro);

        std::cout << "Configuracao carregada com sucesso!\n"
            << "Algoritmo: " << config.algoritmo << " | CPUs: " << config.quantidadeCpus
            << " | Quantum: " << config.quantum << "\n"
            << "Tarefas aperiodicas ignoradas: " << config.tarefasAperiodicasIgnoradas << "\n\n";

        std::cout << "Escolha o Modo de Execucao:\n"
            << "1. Execucao Passo-a-Passo (Debugger)\n"
            << "2. Execucao Completa\n"
            << "Opcao: ";

        int opcao = 1;
        std::cin >> opcao;
        std::cin.ignore();

        Simulador simulador(config);

        if (opcao == 1) {
            exibirAjuda();
            std::string comando;

            while (true) {
                const auto& est = simulador.getEstadoAtual();
                std::cout << "\n================ [ TICK " << est.relogioGlobal << " ] ================\n";

                for (const auto& cpu : est.cpus) {
                    std::cout << " CPU " << cpu.idCPU << ": ";
                    if (cpu.idTarefaAtual != -1) {
                        std::cout << "A executar Tarefa ID " << cpu.idTarefaAtual
                            << " (Ocorrencia " << cpu.numeroOcorrenciaAtual << ")\n";
                    }
                    else {
                        std::cout << "[ OCIOSA / DESLIGADA ]\n";
                    }
                }

                if (simulador.estaConcluida()) {
                    std::cout << "\n--- SIMULACAO CONCLUIDA COM SUCESSO! ---\n";
                    break;
                }

                std::cout << "\nComando (ENTER=avancar, b=voltar, g=exportar, q=sair): ";
                std::getline(std::cin, comando);

                if (comando == "q") break;
                else if (comando == "b") {
                    if (!simulador.retrocederTick()) {
                        std::cout << "Ja se encontra no instante inicial!\n";
                    }
                }
                else if (comando == "g") {
                    simulador.exportarGanttSVG("gantt_passo.svg");
                    std::cout << "Grafico exportado para 'gantt_passo.svg'.\n";
                }
                else {
                    simulador.avancarTick();
                }
            }
        }
        else {
            std::cout << "A executar simualacao completa...\n";
            simulador.executarSimulacaoCompleta();
            std::cout << "Simulacao concluida no tick " << simulador.getEstadoAtual().relogioGlobal << ".\n";
        }

        // Exportação final do Gráfico de Gantt
        simulador.exportarGanttSVG("gantt_final.svg");
        std::cout << "Grafico de Gantt gerado com sucesso: 'gantt_final.svg'\n";

    }
    catch (const std::exception& e) {
        std::cerr << "\nERRO GRAVE: " << e.what() << "\n";
        return 1;
    }

    return 0;
}