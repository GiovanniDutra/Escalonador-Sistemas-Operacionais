#include "LeitorConfig.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
    // Remove espaços em branco do início e do fim do texto para facilitar a leitura dos campos.
    std::string aparar(const std::string& texto) {
        const std::string espacos = " \t\r\n";
        size_t inicio = texto.find_first_not_of(espacos);

        if (inicio == std::string::npos) {
            return "";
        }

        size_t fim = texto.find_last_not_of(espacos);
        return texto.substr(inicio, fim - inicio + 1);
    }

    // Padroniza o nome do algoritmo em maiúsculas para aceitar entradas como "rm" e "RM".
    std::string maiusculas(std::string texto) {
        std::transform(texto.begin(), texto.end(), texto.begin(),
            [](unsigned char caractere) {
                return static_cast<char>(std::toupper(caractere));
            });

        return texto;
    }

    // Divide uma linha pelos pontos e vírgulas e limpa os espaços de cada campo.
    // O último ponto e vírgula é opcional para facilitar a escrita do arquivo.
    std::vector<std::string> separarCampos(const std::string& linha) {
        std::vector<std::string> campos;
        size_t inicio = 0;

        while (true) {
            size_t separador = linha.find(';', inicio);

            if (separador == std::string::npos) {
                campos.push_back(aparar(linha.substr(inicio)));
                break;
            }

            campos.push_back(
                aparar(linha.substr(inicio, separador - inicio))
            );

            inicio = separador + 1;

            // Aceita ponto e virgula opcional no fim da linha.
            if (inicio == linha.size()) {
                break;
            }
        }

        return campos;
    }

    // Converte um campo para inteiro e gera um erro identificando o campo se a conversão falhar.
    int lerInteiro(const std::string& texto, const std::string& nomeCampo) {
        try {
            size_t caracteresLidos = 0;
            int valor = std::stoi(texto, &caracteresLidos);

            if (caracteresLidos != texto.size()) {
                throw std::runtime_error("");
            }

            return valor;
        } catch (...) {
            throw std::runtime_error(
                "O campo \"" + nomeCampo + "\" deve ser um numero inteiro."
            );
        }
    }
}

// Lê e valida o cabeçalho e as tarefas do arquivo antes de iniciar a simulação.
// Tarefas periódicas são armazenadas; as aperiódicas são contabilizadas e ignoradas
// porque não fazem parte do escopo deste projeto.
ConfiguracaoSistema carregarConfiguracao(const std::string& caminho) {
    std::ifstream arquivo(caminho);

    if (!arquivo.is_open()) {
        throw std::runtime_error(
            "Nao consegui abrir esse arquivo. Confira o caminho."
        );
    }

    ConfiguracaoSistema configuracao{};
    std::unordered_set<int> ids;
    std::string linha;
    int numeroLinha = 0;
    bool leuCabecalho = false;
    bool encontrouLinhaDeTarefa = false;

    // Lê o arquivo linha a linha. Linhas vazias são ignoradas e as demais
    // são separadas pelos pontos e vírgulas antes de validar seus campos.
    while (std::getline(arquivo, linha)) {
        numeroLinha++;
        linha = aparar(linha);

        if (linha.empty()) {
            continue;
        }

        std::vector<std::string> campos = separarCampos(linha);

        try {
            if (!leuCabecalho) {

                // A primeira linha define os parâmetros globais: algoritmo, quantum
                // e quantidade de CPUs. Ela é validada separadamente das linhas de tarefa.
                if (campos.size() != 3) {
                    throw std::runtime_error(
                        "A primeira linha deve ter: algoritmo;quantum;qtde_cpus."
                    );
                }

                configuracao.algoritmo = maiusculas(campos[0]);

                if (configuracao.algoritmo != "RM" &&
                    configuracao.algoritmo != "EDF") {
                    throw std::runtime_error(
                        "O algoritmo deve ser RM ou EDF."
                    );
                }

                configuracao.quantum = lerInteiro(campos[1], "quantum");
                configuracao.quantidadeCpus =
                    lerInteiro(campos[2], "qtde_cpus");

                if (configuracao.quantum <= 0) {
                    throw std::runtime_error(
                        "O quantum deve ser maior que zero."
                    );
                }

                if (configuracao.quantidadeCpus <= 0) {
                    throw std::runtime_error(
                        "A quantidade de CPUs deve ser maior que zero."
                    );
                }

                leuCabecalho = true;
                continue;
            }
            
            // Depois do cabeçalho, cada linha não vazia deve representar uma tarefa.
            encontrouLinhaDeTarefa = true;

            if (campos.size() != 6 && campos.size() != 7) {
                throw std::runtime_error(
                    "A tarefa deve ter 6 ou 7 campos: "
                    "id;cor;ingresso;duracao;periodo;prazo;eventos."
                );
            }

            // No Projeto A, a lista de eventos pode estar vazia.
            if (campos.size() == 6) {
                campos.push_back("");
            }

            Tarefa tarefa{};
            tarefa.id = lerInteiro(campos[0], "id");
            tarefa.cor = campos[1];
            tarefa.ingresso = lerInteiro(campos[2], "ingresso");
            tarefa.duracao = lerInteiro(campos[3], "duracao");
            tarefa.periodo = lerInteiro(campos[4], "periodo");
            tarefa.prazo = lerInteiro(campos[5], "prazo");
            tarefa.eventos = campos[6];

            if (tarefa.id <= 0) {
                throw std::runtime_error("O ID deve ser maior que zero.");
            }

            if (!ids.insert(tarefa.id).second) {
                throw std::runtime_error("Esse ID de tarefa ja foi usado.");
            }

            if (tarefa.cor.size() != 6 ||
                !std::all_of(tarefa.cor.begin(), tarefa.cor.end(),
                    [](unsigned char caractere) {
                        return std::isxdigit(caractere) != 0;
                    })) {
                throw std::runtime_error(
                    "A cor deve ter 6 digitos hexadecimais, como FF0000."
                );
            }

            if (tarefa.ingresso < 0 ||
                tarefa.duracao <= 0 ||
                tarefa.prazo <= 0) {
                throw std::runtime_error(
                    "Ingresso nao pode ser negativo; "
                    "duracao e prazo devem ser positivos."
                );
            }

            if (tarefa.periodo < 0) {
                throw std::runtime_error(
                    "O periodo nao pode ser negativo."
                );
            }

            // No Projeto A, tarefas com período zero são aperiódicas e não entram
            // na simulação periódica; o contador permite informar quantas foram ignoradas.
            if (tarefa.periodo == 0) {
                configuracao.tarefasAperiodicasIgnoradas++;
                continue;
            }

            configuracao.tarefas.push_back(tarefa);
        } 
        // Acrescenta o número da linha à mensagem para facilitar a correção
        // do arquivo quando algum campo estiver inválido.
        catch (const std::exception& erro) {
            throw std::runtime_error(
                "Erro na linha " + std::to_string(numeroLinha) +
                ": " + erro.what()
            );
        }
    }

    // Após a leitura, confirma que havia um cabeçalho e ao menos uma tarefa;
    // sem esses dados, não existe configuração válida para iniciar.
    if (!leuCabecalho) {
        throw std::runtime_error(
            "O arquivo esta vazio ou nao tem a primeira linha."
        );
    }

    if (!encontrouLinhaDeTarefa) {
        throw std::runtime_error(
            "O arquivo nao contem nenhuma tarefa."
        );
    }

    return configuracao;
}