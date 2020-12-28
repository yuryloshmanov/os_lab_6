#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <utility>
#include <memory.h>
#include <pthread.h>
#include <zmq.h>


#include "../mq_tools.hpp"


int create(int64_t clientId, int64_t parentId) {
    if (parentId == id) {
        pid_t pid = fork();
        if (pid == -1) {
            fprintf(stderr, "fork failed\n");
            return -1;
        } else if (pid == 0) {
            if (id == -1) {
                char arg1[20];
                snprintf(arg1, 20, "%lld", clientId);

                execl("client", "client", arg1, nullptr);
            } else {
                char arg1[20];
                char arg2[20];
                snprintf(arg1, 20, "%lld", clientId);
                snprintf(arg2, 20, "%lld", parentId);

                execl("client", "client", arg1, arg2, nullptr);
            }

            fprintf(stderr, "execl error\n");
            exit(1);
        }

        sleep(1);

        char buff[41];
        char buff2[42];

        if (id == -1) {
            sprintf(buff, "ipc://_%lld.ipc", clientId);
            sprintf(buff2, "ipc://_%lld_.ipc", clientId);
        } else {
            sprintf(buff, "ipc://%llu_%llu.ipc", parentId, clientId);
            sprintf(buff2, "ipc://%llu_%llu_.ipc", parentId, clientId);
        }

        void *majorSock = zmq_socket(context, ZMQ_REQ);
        void *minorSock = zmq_socket(context, ZMQ_REP);

        int time = MAJOR_SOCKET_RCVTIMEO;
        int time2 = MINOR_SOCKET_RCVTIMEO;

        zmq_setsockopt(majorSock, ZMQ_RCVTIMEO, &time, sizeof time);
        zmq_setsockopt(minorSock, ZMQ_RCVTIMEO, &time2, sizeof time2);


        for (int i = 0; i < 4; i++) {
            if (i == 3) {
                fprintf(stderr, "zmq_connect error\n");
                return -1;
            }

            if ((zmq_connect(majorSock, buff) == -1) || (zmq_connect(minorSock, buff2) == -1)) {
                sleep(2);
            } else {
                break;
            }
        }

        ClientInfo info(clientId, majorSock, minorSock);
        clients.push_back(info);

        auto *thread = new pthread_t;
        threads.push_back(thread);
        auto *newInfo = (ClientInfo *) malloc(sizeof info);
        *newInfo = info;

        pthread_create(threads[threads.size() - 1], nullptr, clientMonitor, (void *) newInfo);
        return pid;
    } else {
        for (const auto &client: clients) {
            Message message(clientId, parentId, CreateCmd);
            sendMessage(client.majorSocket, message);

            Message received_message;
            int status = receiveMessage(client.majorSocket, &received_message);
            if (status == -1) {
                fprintf(stderr, "receive message error\n");
            }
        }

        return 0;
    }
}


int remove(int64_t clientId, uint64_t taskId) {
    if (clientId == id) {
        return clientId;
    }

    for (const auto &client: clients) {
        Message msg(clientId, RemoveCmd, taskId);
        sendMessage(client.majorSocket, msg);
        receiveMessage(client.majorSocket, &msg);
    }
    return 0;
}

int exec(int64_t clientId, ExecStatus execStatus, std::string key, int64_t value, uint64_t taskId) {
    if (clientId == id) {
        return clientId;
    }

    Message msg;
    msg.command = ExecCmd;
    msg.clientId = clientId;
    msg.execStatus = execStatus;
    msg.key = std::move(key);
    msg.value = value;
    msg.taskId = taskId;

    for (const auto &client: clients) {
        sendMessage(client.majorSocket, msg);
        receiveMessage(client.majorSocket, &msg);
    }

    return 0;
}


int ping(int64_t clientId, uint64_t taskId) {
    if (clientId == id) {
        return clientId;
    }

    Message msg(clientId, PingCmd, taskId);
    for (const auto &client: clients) {
        sendMessage(client.majorSocket, msg);
        receiveMessage(client.majorSocket, &msg);
    }

    return 0;
}


int sendMessage(void *socket, Message message) {
    zmq_msg_t sendingMessage;
    zmq_msg_init_size(&sendingMessage, sizeof message);
    memcpy(zmq_msg_data(&sendingMessage), &message, sizeof message);

    int status = zmq_msg_send(&sendingMessage, socket, ZMQ_NOBLOCK);
    zmq_msg_close(&sendingMessage);
    return status;
}

int receiveMessage(void *socket, Message *message) {
    zmq_msg_t receivingMessage;
    zmq_msg_init(&receivingMessage);
    if (zmq_msg_recv(&receivingMessage, socket, 0) == -1) {
        if (errno == EAGAIN) {
            fprintf(stderr, "timeout error\n");
            return 0;
        }
        return -1;
    }

    auto *masterMessage = (Message *)zmq_msg_data(&receivingMessage);
    *message = *masterMessage;
    zmq_msg_close(&receivingMessage);

    return sizeof(*message);
}
