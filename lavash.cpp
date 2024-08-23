#include <cstddef>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <iostream>
#include <cstdio>
#include <assert.h>
#include <fcntl.h>

enum o_type {
    pip = 0,
    l_arr = 1,
    r_arr = 2,
    ili = 3,
    ii = 4,
    prog = 5,
    file = 6, 
};

struct Object {
    o_type type;
    std::string command;
    std::vector<std::string> argv;
    
    Object (o_type t_) {
        type = t_;
    }
    Object (o_type t_, std::string command_) :type(t_), command(command_) {
        type = t_;
        command = command_;
    }
    Object (o_type t_, std::string command_, std::vector<std::string> argv_) : 
        type(t_), command(command_), argv(argv_){}
};

std::vector<Object> argumentsParser(char **argv) {
    std::vector<Object> parsed;
    bool prog = false;
    bool sk = false;
    bool sl = false;
    int last_ind = 0;
    std::string arg = "";
    for (int i = 0; argv[2][i] != '\0'; i++) {
        char t = argv[2][i];
        if (t == '\\') {
            if (argv[2][i + 1] == '\\') {
                arg += '\\';
                i++;
                sl = false;
                continue;
            }
            sl = true;
        } else if (t == '|' && argv[2][i + 1] != '|') {
            parsed.emplace_back(o_type::pip);
            prog = false;
        } else if (t == '|') {
            parsed.emplace_back(o_type::ili);
            prog = false;
            i++;
        } else if (t == '&') {
            parsed.emplace_back(o_type::ii);
            prog = false;
            i++;
        } else if (t == '>') {
            if (i != 0) {
                if (argv[2][i - 1] == '"') {
                    parsed[last_ind].argv.push_back(">");
                    continue;
                }
            }
            parsed.emplace_back(o_type::r_arr);
            prog = false;
        } else if (t == '<') {
            parsed.emplace_back(o_type::l_arr);
            prog = false;
        } else if (t == '"') {
            if (sl && argv[2][i - 1] == '\\') {
                arg += t;
                continue;
            }
            if (sk) {
                sk = false;
            } else {
                sk = true;
                continue;
            }
            if (prog && arg != "") {
                parsed[last_ind].argv.push_back(arg);
            } else if (arg != "") {
                last_ind = parsed.size();
                parsed.emplace_back(o_type::prog, arg);
                prog = true;
                if (arg.find('.') != std::string::npos && arg.find("./") == std::string::npos) {
                    prog = false;
                }
            }
            arg = "";
        } else if (t == ' ') {
            if (sk) {
                arg += t;
            } else if (arg != "") {
                if (prog) {
                    parsed[last_ind].argv.push_back(arg);
                } else {
                    last_ind = parsed.size();
                    parsed.emplace_back(o_type::prog, arg);
                    prog = true;
                    if (arg.find('.') != std::string::npos && arg.find("./") == std::string::npos) {
                        prog = false;
                    }
                }
                arg = "";
            }
        } else {
            arg += t;
        }
      
    }
    if (arg != "") {
        if (prog) {
            parsed[last_ind].argv.push_back(arg);
        } else {
            parsed.emplace_back(o_type::prog, arg);
            prog = true;
        }
    }
    return parsed;
}

int execute_proc(std::vector<Object> proc) {
    size_t proc_ind;
    for (size_t i = 0; i < proc.size(); ++i) {
        int fd, ffd;
        if (proc[i].type == o_type::l_arr) {
            fd = open(proc[i + 1].command.c_str(), O_RDONLY);
            proc[i + 1].type = o_type::file;
            ffd = 0;
        } else if (proc[i].type == o_type::r_arr) {
            fd = open(proc[i + 1].command.c_str(),  O_WRONLY | O_CREAT | O_TRUNC, 0644);
            proc[i + 1].type = o_type::file;
            ffd = 1;
        } else if (proc[i].type == o_type::prog) {
            proc_ind = i;
        }
        if (fd < 0) {
            std::cerr << "./lavash: line 1: " << proc[i + 1].command << ": No such file or directory\n";
            return EXIT_FAILURE;
        }
        dup2(fd, ffd);
        close(fd);
    }

    /// exec process
    Object o = proc[proc_ind];
    std::vector<char*> combined;
    combined.reserve(o.argv.size() + 2);
    combined.push_back((char*)o.command.data());
    for (size_t i = 0; i < o.argv.size(); ++i) {
        combined.push_back((char*)o.argv[i].data());
    }
    combined.push_back(nullptr);
    //////////
    execvp(o.command.c_str(), combined.data());
    std::cerr << "./lavash: line 1: " << o.command << ": command not found\n";
    return 127;
}

int parsePipelines(size_t pos, std::vector<std::vector<Object>> pipelines) {
    if (pos == pipelines.size() - 1) {
        return execute_proc(pipelines[pos]);
    }

    int ret_status;
    int fd[2];

    if (pipe(fd) < 0) {
        exit(EXIT_FAILURE);
    }

    pid_t pid1;
    if ((pid1 = fork()) < 0) {
        exit(EXIT_FAILURE);
    }

    if (pid1 == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        exit(execute_proc(pipelines[pos]));
    }

    pid_t pid2;
    if ((pid2 = fork()) < 0) {
        exit(EXIT_FAILURE);
    }

    if (pid2 == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        exit(parsePipelines(pos + 1, pipelines));
    }

    close(fd[0]);
    close(fd[1]);

    int status;
    pid_t pid3;
    while ((pid3 = wait(&status)) != -1) {
        if (pid3 == pid2) {
            ret_status = status;
        }
    }
    return ret_status;
}

int parsePartsTopipelines(std::vector<Object> &cmds) {
    std::vector<Object> tmp;
    std::vector<std::vector<Object>> pipelines;
    for (size_t i = 0; i < cmds.size(); ++i) {
        if (cmds[i].type == o_type::pip) {
            pipelines.push_back(tmp);
            tmp.clear();
        } else {
            tmp.push_back(cmds[i]);
        }
    }
    if (!tmp.empty()) {
        pipelines.push_back(tmp);
        tmp.clear();
    }

    return parsePipelines(0, pipelines);
}

int main(int argc, char **argv, char **envv) {
    std::vector<Object> args = argumentsParser(argv);
    std::vector<Object> tmp;
    std::string str = "";
    std::vector<std::pair<std::vector<Object>, o_type> > whole_process;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].type == o_type::ili || args[i].type == o_type::ii) {
            whole_process.push_back(make_pair(tmp, args[i].type));
            tmp.clear();
        } else {
            tmp.push_back(args[i]);
        }
    }
    if (!tmp.empty()) {
        whole_process.push_back(make_pair(tmp, o_type::file));
        tmp.clear();
    }
    
    for (size_t i = 0; i < whole_process.size(); ++i) {
        pid_t pid;
        if ((pid = fork()) < 0) {
            exit(EXIT_FAILURE);
        }

        if (!pid) {
            exit(parsePartsTopipelines(whole_process[i].first));
        }

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            status = WEXITSTATUS(status);
        }

        for (; i < whole_process.size(); ++i) {
            if (whole_process[i].second == o_type::ii) {
                if (status == EXIT_SUCCESS) {
                    break;
                }
            } else if (whole_process[i].second == o_type::ili) {
                if (status != EXIT_SUCCESS) {
                    break;
                }
            } else {
                return status;
            }
        }
    }

    return 0;
}
