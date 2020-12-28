#ifndef LAB6_MQ_TOOLS_H
#define LAB6_MQ_TOOLS_H


#include <algorithm>
#include <unistd.h>
#include <string>
#include <vector>


#define MAJOR_SOCKET_RCVTIMEO 5 * 1000
#define MINOR_SOCKET_RCVTIMEO 5 * 1000


struct ClientInfo {
    int64_t id;
    void *majorSocket;
    void *minorSocket;

    ClientInfo(
            int64_t id,
            void *majorSocket,
            void *minorSocket
    ) : id(id), majorSocket(majorSocket), minorSocket(minorSocket) {}
};


enum Command {
    CreateCmd,
    RemoveCmd,
    ExecCmd,
    PingCmd,
    HeartbeatCmd,
};


enum Status {
    OkStatus,
    DoneStatus,
    ErrorStatus
};


enum ExecStatus {
    GetExec,
    SetExec,
    NotFoundExec
};


struct Message {
    int64_t clientId{};
    int64_t parentId{};
    std::string key{};
    int64_t value{};
    Command command{};
    Status status{};
    uint64_t taskId{};
    time_t time{};
    ExecStatus execStatus{};

    Message() = default;

    Message(
            int64_t clientId,
            Command command,
            Status status
    ) : clientId(clientId), command(command), status(status) {}

    Message(
            int64_t clientId,
            int64_t parentId,
            Command command
    ) : clientId(clientId), parentId(parentId), command(command) {}

    Message(
            int64_t clientId,
            Command command,
            uint64_t taskId
    ) : clientId(clientId), command(command), taskId(taskId) {}

    Message(
            int64_t clientId,
            Command command,
            uint64_t taskId,
            time_t time
    ) : clientId(clientId), command(command), taskId(taskId), time(time) {}
};


extern int64_t id;
extern void *context;
extern std::vector<ClientInfo> clients;
extern std::vector<pthread_t *> threads;

int create(int64_t clientId, int64_t parentId);

int remove(int64_t clientId, uint64_t taskId);

int ping(int64_t clientId, uint64_t taskId);

int exec(int64_t clientId, ExecStatus execStatus, std::string key, int64_t value, uint64_t taskId);

void *clientMonitor(void *params);

int sendMessage(void *socket, Message message);

int receiveMessage(void *socket, Message *message);


#endif //LAB6_MQ_TOOLS_H
