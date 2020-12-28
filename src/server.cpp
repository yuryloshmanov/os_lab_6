#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>
#include <cstdio>
#include <deque>
#include <ctime>
#include <zmq.h>
#include <set>


#include "lib/mq_tools.hpp"


#define MAXIMUM_COMMAND_EXECUTION_TIME 10


std::vector<pthread_t *> threads;
std::vector<ClientInfo> clients;
std::set<int64_t> ids;
std::deque<Message> tasks;

int64_t id{-1};
int64_t task_id_{0};

pthread_t takeUnfinishedTaskThread;
pthread_mutex_t taskIdMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tasksMutex = PTHREAD_MUTEX_INITIALIZER;


void *context;


int64_t taskId() {
    pthread_mutex_lock(&taskIdMutex);

    auto result = task_id_++;

    pthread_mutex_unlock(&taskIdMutex);
    return result;
}


void *takeUnfinishedTask(void *) {
    while (true) {
        sleep(MAXIMUM_COMMAND_EXECUTION_TIME);
        if (pthread_mutex_lock(&tasksMutex)) {
            printf("lock error\n");
            break;
        }

        if (!tasks.empty() && (time(nullptr) - tasks.front().time) >= MAXIMUM_COMMAND_EXECUTION_TIME) {
            auto task = tasks.front();
            switch (task.command) {
                case CreateCmd: {
                    printf("Error: Parent is unavailable\n");
                    break;
                }
                case RemoveCmd:
                case ExecCmd: {
                    printf("Error: Node is unavailable\n");
                    break;
                }
                case PingCmd: {
                    printf("Ok: 0\n");
                    break;
                }
                default: {
                    break;
                }
            }
            tasks.pop_front();
        }
        if (pthread_mutex_unlock(&tasksMutex)) {
            printf("unlock error\n");
            break;
        }
    }
    return nullptr;
}


void *clientMonitor(void *params) {
    auto *client = (ClientInfo *) params;

    while (true) {
        Message msg;
        if (receiveMessage(client->minorSocket, &msg) == -1) {
            break;
        }

        if (sendMessage(client->minorSocket, msg) == -1) {
            break;
        }

        pthread_mutex_lock(&tasksMutex);
        switch (msg.command) {
            case CreateCmd: {
                switch (msg.status) {
                    case DoneStatus: {
                        for (const auto& task: tasks) {
                            if (task.clientId == msg.clientId) {
                                printf("Ok: %lld\n", msg.value);
                                ids.insert(task.clientId);
                                break;
                            }
                        }
                        auto end = std::remove_if(tasks.begin(), tasks.end(), [&](auto task) {
                            return task.clientId == msg.clientId;
                        });
                        tasks.erase(end, tasks.end());
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case RemoveCmd:
            case ExecCmd:
            case PingCmd: {
                switch (msg.status) {
                    case DoneStatus: {
                        for (const auto& task: tasks) {
                            if (task.taskId == msg.taskId) {
                                if (msg.command == RemoveCmd) {
                                    printf("Ok\n");
                                } else if (msg.command == ExecCmd) {
                                    if (msg.execStatus == GetExec) {
                                        printf("Ok:%lld: %lld\n", msg.clientId, msg.value);
                                    } else if (msg.execStatus == SetExec) {
                                        printf("Ok:%lld\n", msg.clientId);
                                    } else {
                                        printf("Ok:%lld:%s not found\n", msg.clientId, msg.key.c_str());
                                    }
                                } else {
                                    printf("Ok: 1\n");
                                }

                                if (msg.command == RemoveCmd) {
                                    ids.erase(task.clientId);
                                }
                                break;
                            }
                        }
                        auto end = std::remove_if(tasks.begin(), tasks.end(), [&](auto task) {
                            return task.taskId == msg.taskId;
                        });
                        tasks.erase(end, tasks.end());

                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }
        pthread_mutex_unlock(&tasksMutex);

        if (msg.clientId == client->id && msg.command == RemoveCmd && msg.status == DoneStatus) {
            break;
        }
    }

    zmq_close(client->majorSocket);
    zmq_close(client->minorSocket);

    auto end = std::remove_if(clients.begin(), clients.end(), [&](auto clt) {
        return clt.id == client->id;
    });
    clients.erase(end, clients.end());

    free(params);
    return nullptr;
}


int main() {
    pthread_create(&takeUnfinishedTaskThread, nullptr, takeUnfinishedTask, nullptr);

    context = zmq_ctx_new();

    for (std::string line; std::getline(std::cin, line);) {
        std::stringstream ss(line);
        std::string command;
        if (!(ss >> command)) {
            std::cout << "Error: Invalid input" << std::endl;
            continue;
        }

        if (command == "create") {
            int64_t clientId;
            int64_t parentId;

            if (!(ss >> clientId) || !(ss >> parentId)) {
                std::cout << "Error: Invalid input" << std::endl;
                continue;
            }

            if (clientId < 0) {
                printf("Error: Invalid client id\n");
                continue;
            } else if (parentId < -1) {
                printf("Error: Invalid parent id\n");
                continue;
            }

            if (ids.find(clientId) != ids.end()) {
                printf("Error: Already exists\n");
                continue;
            }

            if (parentId != -1 && ids.find(parentId) == ids.end()) {
                printf("Error: Parent not found\n");
                continue;
            }

            Message msg;
            msg.clientId = clientId;
            msg.command = CreateCmd;
            msg.time = time(nullptr);

            pthread_mutex_lock(&tasksMutex);
            tasks.push_back(msg);
            pthread_mutex_unlock(&tasksMutex);

            int status = create(clientId, parentId);
            if (status == -1) {
                printf("Error: Can't create child client\n");
                tasks.pop_back();
            }
        } else if (command == "remove") {
            int64_t clientId;

            if (!(ss >> clientId)) {
                std::cout << "Error: Invalid input" << std::endl;
                continue;
            }

            if (clientId < 0) {
                printf("Error: Invalid client id\n");
                continue;
            }

            if (ids.find(clientId) == ids.end()) {
                printf("Error: Not found\n");
                continue;
            }

            auto tskId = taskId();
            Message msg;
            msg.clientId = clientId;
            msg.command = RemoveCmd;
            msg.taskId = tskId;
            msg.time = time(nullptr);

            pthread_mutex_lock(&tasksMutex);
            tasks.push_back(msg);
            pthread_mutex_unlock(&tasksMutex);

            remove(clientId, tskId);
        } else if (command == "exec") {
            int64_t clientId;
            std::string key;
            int64_t value;
            bool flag = true;

            if (!(ss >> clientId)) {
                std::cout << "Error: Invalid input" << std::endl;
                continue;
            }

            if (!(ss >> key)) {
                std::cout << "Error: Invalid key" << std::endl;
                continue;
            }

            if (!(ss >> value)) {
                flag = false;
            }

            if (clientId < 0) {
                printf("Error: Invalid client id\n");
                continue;
            }

            if (ids.find(clientId) == ids.end()) {
                printf("Error: Not found\n");
                continue;
            }

            Message msg(clientId, ExecCmd, taskId(), time(nullptr));
            if (flag) {
                msg.execStatus = SetExec;
            } else {
                msg.execStatus = GetExec;
            }

            pthread_mutex_lock(&tasksMutex);
            tasks.push_back(msg);
            pthread_mutex_unlock(&tasksMutex);

            exec(clientId, msg.execStatus, key, value, msg.taskId);
        } else if (command == "ping") {
            int64_t clientId;

            if (!(ss >> clientId)) {
                std::cout << "Error: Invalid input" << std::endl;
                continue;
            }

            if (clientId < 0) {
                printf("Error: Invalid client id\n");
                continue;
            }

            if (ids.find(clientId) == ids.end()) {
                printf("Error: Not found\n");
                continue;
            }

            Message msg(clientId, PingCmd, taskId(), time(nullptr));

            pthread_mutex_lock(&tasksMutex);
            tasks.push_back(msg);
            pthread_mutex_unlock(&tasksMutex);

            ping(clientId, msg.taskId);
        } else if (command == "exit") {
            break;
        } else {
            printf("Error: Invalid command\n");
        }
    }

    zmq_ctx_destroy(context);
    return 0;
}