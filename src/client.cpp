#include <pthread.h>
#include <unistd.h>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>
#include <cstdio>
#include <deque>
#include <ctime>
#include <zmq.h>
#include <map>


#include "lib/mq_tools.hpp"


#define MINOR_SOCKET_MONITOR_SLEEP_TIME 1


std::map<std::string, int64_t> dictionary;
int64_t id;

void *context;
void *majorSocket;
void *minorSocket;

pthread_t majorThread;
pthread_t minorThread;

std::vector<pthread_t *> threads;
std::vector<ClientInfo> clients;


bool flag{true};
Message messageToMaster;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void setMessage(Message message) {
    pthread_mutex_lock(&mutex);

    messageToMaster = std::move(message);
    flag = true;

    pthread_mutex_unlock(&mutex);
}

bool get_message(Message *message) {
    bool result;
    pthread_mutex_lock(&mutex);

    result = flag;

    if (flag) {
        *message = messageToMaster;
        flag = false;
    }

    pthread_mutex_unlock(&mutex);

    return result;
}


void *clientMonitor(void *params) {
    auto *client = (ClientInfo *)params;

    while (true) {
        Message msg;
        if (receiveMessage(client->minorSocket, &msg) == -1) {
            break;
        }

        if (sendMessage(client->minorSocket, msg) == -1) {
            break;
        }
        if (msg.command == HeartbeatCmd) {
            continue;
        }

        switch (msg.status) {
            case DoneStatus:
            case ErrorStatus: {
                setMessage(msg);
                break;
            }
            default:
                break;
        }

        if (msg.clientId == client->id && msg.command == RemoveCmd && msg.status == DoneStatus) {
            break;
        }
    }

    zmq_close(client->majorSocket);
    zmq_close(client->minorSocket);

    auto end = std::remove_if(clients.begin(), clients.end(), [&](auto clt){
        return clt.id == client->id;
    });
    clients.erase(end, clients.end());

    free(params);
    return nullptr;
}


void *major_socket_monitor(void *) {
    while (true) {
        Message message;
        receiveMessage(majorSocket, &message);

        message.status = OkStatus;
        switch (message.command) {
            case CreateCmd: {
                sendMessage(majorSocket, message);

                create(message.clientId, message.parentId);
                break;
            }
            case RemoveCmd:
                sendMessage(majorSocket, message);
                if (remove(message.clientId, message.taskId) == id) {
                    message.status = DoneStatus;
                    setMessage(message);
                    return nullptr;
                }
                break;
            case ExecCmd: {
                sendMessage(majorSocket, message);
                if (exec(message.clientId, message.execStatus, message.key, message.value, message.taskId) == id) {
                    if (message.execStatus == GetExec) {
                        if (dictionary.find(message.key) == dictionary.end()) {
                            message.execStatus = NotFoundExec;
                        } else {
                            message.value = dictionary.at(message.key);
                        }
                    } else {
                        dictionary[message.key] = message.value;
                    }

                    message.status = DoneStatus;
                    setMessage(message);
                }
                break;
            }
            case PingCmd: {
                sendMessage(majorSocket, message);
                if (ping(message.clientId, message.taskId) == id) {
                    message.status = DoneStatus;
                    setMessage(message);
                }
                break;
            }
            default: {
                message.status = ErrorStatus;
                sendMessage(majorSocket, message);
                break;
            }
        }
    }
}


void *minor_socket_monitor(void *) {
    sleep(1);
    flag = true;
    messageToMaster.clientId = id;
    messageToMaster.value = getpid();
    messageToMaster.status = DoneStatus;

    while (true) {
        Message msg(id, HeartbeatCmd, OkStatus);

        get_message(&msg);
        if (sendMessage(minorSocket, msg) == -1) {
            break;
        }

        if (receiveMessage(minorSocket, &msg) == -1) {
            break;
        }

        if (msg.command == RemoveCmd && msg.status == DoneStatus && msg.clientId == id) {
            break;
        }

        sleep(MINOR_SOCKET_MONITOR_SLEEP_TIME);
    }

    pthread_kill(majorThread, SIGKILL);
    return nullptr;
}


int main(int argc, char *argv[]) {
    id = std::stoll(argv[1]);

    context = zmq_ctx_new();
    majorSocket = zmq_socket(context, ZMQ_REP);
    minorSocket = zmq_socket(context, ZMQ_REQ);

    if (argc == 2) {
        char buff[31];
        char buff2[32];

        sprintf(buff, "ipc://_%lld.ipc", id);
        sprintf(buff2, "ipc://_%lld_.ipc", id);

        zmq_bind(majorSocket, buff);

        int time = MINOR_SOCKET_RCVTIMEO;
        zmq_setsockopt(minorSocket, ZMQ_RCVTIMEO, &time, sizeof time);
        zmq_bind(minorSocket, buff2);
    } else if (argc == 3) {
        char buff[41];
        char buff2[42];

        sprintf(buff, "ipc://%llu_%llu.ipc", std::stoll(argv[2]), id);
        sprintf(buff2, "ipc://%llu_%llu_.ipc", std::stoll(argv[2]), id);

        zmq_bind(majorSocket, buff);

        int time = MINOR_SOCKET_RCVTIMEO;
        zmq_setsockopt(minorSocket, ZMQ_RCVTIMEO, &time, sizeof time);
        zmq_bind(minorSocket, buff2);
    } else {
        fprintf(stderr, "invalid arguments\n");
        exit(1);
    }

    pthread_create(&majorThread, nullptr, major_socket_monitor, nullptr);
    pthread_create(&minorThread, nullptr, minor_socket_monitor, nullptr);

    pthread_join(majorThread, nullptr);
    pthread_join(minorThread, nullptr);

    zmq_close(majorSocket);
    zmq_close(minorSocket);

    zmq_ctx_destroy(context);
    return 0;
}
