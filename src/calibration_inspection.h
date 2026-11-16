// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <cJSON.h>

// Local-only, versioned inspection interface. Commands execute on the app thread.
// No arbitrary evaluation, file paths, device injection or acceptance overrides.
class CalibrationInspection {
    struct Request {
        std::string input,output;
        std::mutex mutex;std::condition_variable changed;
        bool done=false,expired=false;
    };
    std::atomic<bool> stopping{false};
    std::thread server;
    std::mutex mutex;
    std::queue<std::shared_ptr<Request>> queue;
    std::string state="{\"schema\":1,\"phase\":\"starting\"}";
    std::wstring pipe;
    void serve() {
        HANDLE handle=CreateNamedPipeW(pipe.c_str(),PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,
            1,65536,65536,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return;
        while(!stopping) {
            const BOOL connected=ConnectNamedPipe(handle,nullptr);
            if(!connected&&GetLastError()!=ERROR_PIPE_CONNECTED){Sleep(10);continue;}
            char buffer[32768];DWORD bytes=0;bool read=false;
            const auto start=std::chrono::steady_clock::now();
            while(!stopping&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2)) {
                if(ReadFile(handle,buffer,sizeof(buffer)-1,&bytes,nullptr)&&bytes){read=true;break;}
                Sleep(5);
            }
            std::string response="{\"ok\":false,\"error\":\"request timeout\"}";
            if(read) {
                buffer[bytes]=0;cJSON* input=cJSON_Parse(buffer);
                const cJSON* operation=input ? cJSON_GetObjectItemCaseSensitive(input,"op"):nullptr;
                if(cJSON_IsString(operation)&&std::string(operation->valuestring)=="state") {
                    std::lock_guard<std::mutex> lock(mutex);response=state;
                } else if(input) {
                    auto request=std::make_shared<Request>();request->input=buffer;
                    {std::lock_guard<std::mutex> lock(mutex);queue.push(request);}
                    std::unique_lock<std::mutex> lock(request->mutex);
                    request->changed.wait_for(lock,std::chrono::seconds(5),[&]{return request->done||stopping;});
                    if(request->done)response=request->output;
                    else{request->expired=true;response="{\"ok\":false,\"error\":\"app did not process command before expiry\"}";}
                } else response="{\"ok\":false,\"error\":\"invalid JSON\"}";
                cJSON_Delete(input);
            }
            DWORD written;WriteFile(handle,response.data(),DWORD(response.size()),&written,nullptr);
            // Client acknowledges reading the message before disconnect. This
            // preserves response delivery without an unbounded blocking flush.
            const auto acknowledgement=std::chrono::steady_clock::now();
            while(std::chrono::steady_clock::now()-acknowledgement<(stopping ? std::chrono::milliseconds(200):std::chrono::milliseconds(2000))) {
                DWORD received=0;if(ReadFile(handle,buffer,sizeof(buffer),&received,nullptr)&&received)break;Sleep(5);
            }
            DisconnectNamedPipe(handle);
        }
        CloseHandle(handle);
    }
public:
    CalibrationInspection() {
        pipe=L"\\\\.\\pipe\\VDXRCalibration."+std::to_wstring(GetCurrentProcessId());
        server=std::thread([this]{serve();});
    }
    ~CalibrationInspection(){stopping=true;if(server.joinable())server.join();}
    void update(const std::string& snapshot){std::lock_guard<std::mutex> lock(mutex);state=snapshot;}
    void process(const std::function<std::string(const std::string&)>& handler) {
        for(int i=0;i<8;++i) {
            std::shared_ptr<Request> request;
            {std::lock_guard<std::mutex> lock(mutex);if(queue.empty())break;request=queue.front();queue.pop();}
            std::lock_guard<std::mutex> lock(request->mutex);
            if(request->expired)continue;
            try{request->output=handler(request->input);}
            catch(...){request->output="{\"ok\":false,\"error\":\"command failed\"}";}
            request->done=true;request->changed.notify_all();
        }
    }
};

inline std::string inspectionJson(cJSON* value) {
    char* text=cJSON_PrintUnformatted(value);std::string result=text ? text:"{}";cJSON_free(text);cJSON_Delete(value);return result;
}
