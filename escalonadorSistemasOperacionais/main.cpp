#include <iostream>
#include <string>
#include <sstream>
#include "LeitorConfig.h"
#include "Simulador.h"

const char* nomeEstado(EstadoOcorrencia estado) {
    switch (estado) {
    case EstadoOcorrencia::Futura:    return "Futura";
    case EstadoOcorrencia::Pronta:    return "Pronta";
    case EstadoOcorrencia::Executando:return "Executando";
    case EstadoOcorrencia::Suspensa:  return "Suspensa";
    case EstadoOcorrencia::Concluida: return "Concluida";
    default:                          return "Desconhecido";
    }
}

void exibirTarefas(const SnapshotSimulacao& estado) {
    std::cout << "\nTarefas no tick " << estado.relogioGlobal << ":\n";

    for (const auto& tarefa : estado.tarefas) {
        std::cout << "\nTarefa " << tarefa.id
            << " | ingresso=" << tarefa.ingresso
            << " | duracao=" << tarefa.duracao
            << " | periodo=" << tarefa.periodo
            << " | prazo=" << tarefa.prazo
            << " | cor=#" << tarefa.cor << "\n";

        for (const auto& oc : tarefa.ocorrencias) {
            std::cout << "  Ocorrencia " << oc.numero
                << " | estado=" << nomeEstado(oc.estado)
                << " | ativacao=" << oc.instanteAtivacao
                << " | deadline=" << oc.deadlineAbsoluto
                << " | restante=" << oc.tempoRestante;

            if (oc.perdeuPrazo) {
                std::cout << " | PRAZO PERDIDO";
            }

            if (oc.cpuAtual >= 0) {
                std::cout << " | CPU=" << oc.cpuAtual;
            }

            std::cout << "\n";
        }
    }
}

void exibirAjuda() {
    std::cout << "\nComandos do Modo Passo-a-Passo:\n"
        << "  [ENTER] : Avanca 1 tick\n"
        << "  b        : Retrocede 1 tick\n"
        << "  l        : Lista tarefas e ocorrencias\n"
        << "  e <id> <ocorrencia> <estado> : Altera o estado de uma ocorrencia\n"
        << "  c <id> <campo> <valor>     : Altera uma caracteristica da tarefa\n"
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
                        std::cout << "[ DESLIGADA ]\n";
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
                else if (comando == "l") {
                    exibirTarefas(simulador.getEstadoAtual());
                }
                else if (comando == "g") {
                    simulador.exportarGanttSVG("gantt_passo.svg");
                    std::cout << "Grafico exportado para 'gantt_passo.svg'.\n";
                }
                else if (comando == "e" || comando.rfind("e ", 0) == 0) {
                    std::istringstream entrada(comando);

                    std::string acao;
                    std::string nomeEstadoNovo;
                    int idTarefa;
                    int numeroOcorrencia;

                    if (!(entrada >> acao >> idTarefa >> numeroOcorrencia >> nomeEstadoNovo)) {
                        std::cout
                            << "Formato: e <id> <ocorrencia> "
                            << "<futura|pronta|executando|suspensa|concluida>\n";
                    } else {
                        EstadoOcorrencia novoEstado;
                        bool estadoValido = true;

                        if (nomeEstadoNovo == "futura") {
                            novoEstado = EstadoOcorrencia::Futura;
                        } else if (nomeEstadoNovo == "pronta") {
                            novoEstado = EstadoOcorrencia::Pronta;
                        } else if (nomeEstadoNovo == "executando") {
                            novoEstado = EstadoOcorrencia::Executando;
                        } else if (nomeEstadoNovo == "suspensa") {
                            novoEstado = EstadoOcorrencia::Suspensa;
                        } else if (nomeEstadoNovo == "concluida") {
                            novoEstado = EstadoOcorrencia::Concluida;
                        } else {
                            estadoValido = false;
                            std::cout
                                << "Estado invalido. Use: futura, pronta, executando, "
                                << "suspensa ou concluida.\n";
                        }

                        if (estadoValido) {
                            try {
                                simulador.alterarEstadoOcorrencia(
                                    idTarefa,
                                    numeroOcorrencia,
                                    novoEstado);

                                std::cout << "Estado alterado. Digite l para conferir.\n";
                            } catch (const std::exception& erro) {
                                std::cout << "Nao foi possivel alterar o estado: "
                                    << erro.what() << "\n";
                            }
                        }
                    }
                }
                else if (comando == "c" || comando.rfind("c ", 0) == 0) {
                    std::istringstream entrada(comando);

                    std::string acao;
                    std::string campo;
                    std::string valor;
                    int idTarefa;

                    if (!(entrada >> acao >> idTarefa >> campo >> valor)) {
                        std::cout
                            << "Formato: c <id> <cor|ingresso|duracao|periodo|prazo> <valor>\n";
                    } else {
                        try {
                            simulador.alterarCaracteristicaTarefa(
                                idTarefa,
                                campo,
                                valor);

                            std::cout << "Caracteristica alterada. Digite l para conferir.\n";
                        } catch (const std::exception& erro) {
                            std::cout << "Nao foi possivel alterar: "
                                << erro.what() << "\n";
                        }
                    }
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

        const auto periodosDesligados =
            simulador.getPeriodosCPUsDesligadas();

        if (periodosDesligados.empty()) {
            std::cout << "Nenhuma CPU ficou desligada durante os ticks executados.\n";
        } 
        else {
            std::cout << "\nPeriodos de CPUs desligadas "
                    << "(intervalo [inicio, fim)):\n";

            for (const auto& periodo : periodosDesligados) {
                std::cout << "CPU " << periodo.idCPU
                        << ": ticks " << periodo.inicio
                        << " a " << periodo.fim << "\n";
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "\nERRO GRAVE: " << e.what() << "\n";
        return 1;
    }

    return 0;
}