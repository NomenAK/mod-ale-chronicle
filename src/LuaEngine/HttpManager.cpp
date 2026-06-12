#include <thread>
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
};

#define CPPHTTPLIB_OPENSSL_SUPPORT

#include "libs/httplib.h"
#include "HttpManager.h"
#include "LuaEngine.h"

HttpWorkItem::HttpWorkItem(int funcRef, const std::string& httpVerb, const std::string& url, const std::string& body, const std::string& contentType, const httplib::Headers& headers)
    : funcRef(funcRef),
    httpVerb(httpVerb),
    url(url),
    body(body),
    contentType(contentType),
    headers(headers)
{ }

HttpResponse::HttpResponse(int funcRef, int statusCode, const std::string& body, const httplib::Headers& headers)
    : funcRef(funcRef),
    statusCode(statusCode),
    body(body),
    headers(headers)
{ }

HttpManager::HttpManager()
    : workQueue(16),
    responseQueue(16),
    startedWorkerThread(false),
    cancelationToken(false),
    condVar(),
    condVarMutex(),
    parseUrlRegex("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?")
{
    StartHttpWorker();
}

HttpManager::~HttpManager()
{
    StopHttpWorker();
}

void HttpManager::PushRequest(HttpWorkItem* item)
{
    std::unique_lock<std::mutex> lock(condVarMutex);
    workQueue.push(item);
    condVar.notify_one();
}

void HttpManager::StartHttpWorker()
{
    ClearQueues();

    if (!startedWorkerThread)
    {
        cancelationToken.store(false);
        workerThread = std::thread(&HttpManager::HttpWorkerThread, this);
        startedWorkerThread = true;
    }
}

void HttpManager::ClearQueues()
{
    while (workQueue.front())
    {
        HttpWorkItem* item = *workQueue.front();
        if (item != nullptr)
        {
            delete item;
        }
        workQueue.pop();
    }

    while (responseQueue.front())
    {
        HttpResponse* item = *responseQueue.front();
        if (item != nullptr)
        {
            delete item;
        }
        responseQueue.pop();
    }
}

void HttpManager::StopHttpWorker()
{
    if (!startedWorkerThread)
    {
        return;
    }

    cancelationToken.store(true);
    condVar.notify_one();
    workerThread.join();
    ClearQueues();
    startedWorkerThread = false;
}

void HttpManager::QueueErrorResponse(int funcRef, const std::string& errorBody)
{
    // Transport-level failure: there is no HTTP status to report. Queue a
    // statusCode 0 response through the same queue as successes, so that
    // HandleHttpResponses still invokes the Lua callback (status 0, error
    // message as body, empty headers) on the Lua-owning thread and releases
    // funcRef. Dropping the request here would leak the callback ref and
    // leave Lua-side failure handling (retry/backoff) unreachable.
    responseQueue.push(new HttpResponse(funcRef, 0, errorBody, httplib::Headers()));
}

void HttpManager::HttpWorkerThread()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(condVarMutex);
            condVar.wait(lock, [&] { return workQueue.front() != nullptr || cancelationToken.load(); });
        }

        if (cancelationToken.load())
        {
            break;
        }
        if (!workQueue.front())
        {
            continue;
        }

        HttpWorkItem* req = *workQueue.front();
        workQueue.pop();
        if (!req)
        {
            continue;
        }

        // Invariant: every dequeued work item queues exactly one HttpResponse
        // (success or error), so HandleHttpResponses always invokes the Lua
        // callback and releases its funcRef.
        try
        {
            std::string host;
            std::string path;

            if (!ParseUrl(req->url, host, path)) {
                ALE_LOG_ERROR("[ALE]: Could not parse URL {}", req->url);
                QueueErrorResponse(req->funcRef, "could not parse URL: " + req->url);
                delete req;
                continue;
            }

            httplib::Client cli(host);
            cli.set_connection_timeout(0, 3000000); // 3 seconds
            cli.set_read_timeout(60, 0); // 60 seconds — covers LLM inference + safety margin
            cli.set_write_timeout(30, 0); // 30 seconds

            httplib::Result res = DoRequest(cli, req, path);
            httplib::Error err = res.error();
            if (err != httplib::Error::Success)
            {
                ALE_LOG_ERROR("[ALE]: HTTP request error: {}", httplib::to_string(err));
                QueueErrorResponse(req->funcRef, httplib::to_string(err));
                delete req;
                continue;
            }

            if (res->status == 301)
            {
                std::string location = res->get_header_value("Location");
                std::string host;
                std::string path;

                if (!ParseUrl(location, host, path))
                {
                    ALE_LOG_ERROR("[ALE]: Could not parse URL after redirect: {}", location);
                    QueueErrorResponse(req->funcRef, "could not parse URL after redirect: " + location);
                    delete req;
                    continue;
                }
                httplib::Client cli2(host);
                cli2.set_connection_timeout(0, 3000000); // 3 seconds
                cli2.set_read_timeout(60, 0); // 60 seconds — covers LLM inference + safety margin
                cli2.set_write_timeout(30, 0); // 30 seconds
                res = DoRequest(cli2, req, path);
                err = res.error();
                if (err != httplib::Error::Success)
                {
                    ALE_LOG_ERROR("[ALE]: HTTP request error after redirect: {}", httplib::to_string(err));
                    QueueErrorResponse(req->funcRef, httplib::to_string(err));
                    delete req;
                    continue;
                }
            }

            responseQueue.push(new HttpResponse(req->funcRef, res->status, res->body, res->headers));
        }
        catch (const std::exception& ex)
        {
            ALE_LOG_ERROR("[ALE]: HTTP request error: {}", ex.what());
            // Nothing was queued for this request (the success push is the last
            // statement of the try block and queueing a HttpResponse* cannot
            // throw once allocated), so this cannot double-invoke the callback.
            QueueErrorResponse(req->funcRef, ex.what());
        }

        delete req;
    }
}

httplib::Result HttpManager::DoRequest(httplib::Client& client, HttpWorkItem* req, const std::string& urlPath)
{
    const char* path = urlPath.c_str();
    if (req->httpVerb == "GET")
    {
        return client.Get(path, req->headers);
    }
    if (req->httpVerb == "HEAD")
    {
        return client.Head(path, req->headers);
    }
    if (req->httpVerb == "POST")
    {
        return client.Post(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "PUT")
    {
        return client.Put(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "PATCH")
    {
        return client.Patch(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "DELETE")
    {
        return client.Delete(path, req->headers);
    }
    if (req->httpVerb == "OPTIONS")
    {
        return client.Options(path, req->headers);
    }

    ALE_LOG_ERROR("[ALE]: HTTP request error: invalid HTTP verb {}", req->httpVerb);
    return client.Get(path, req->headers);
}

bool HttpManager::ParseUrl(const std::string& url, std::string& host, std::string& path)
{
    std::smatch matches;

    if (!std::regex_search(url, matches, parseUrlRegex))
    {
        return false;
    }

    std::string scheme = matches[2];
    std::string authority = matches[4];
    std::string query = matches[7];
    host = scheme + "://" + authority;
    path = matches[5];
    if (path.empty())
    {
        path = "/";
    }
    path += (query.empty() ? "" : "?") + query;

    return true;
}

void HttpManager::HandleHttpResponses()
{
    while (!responseQueue.empty())
    {
        HttpResponse* res = *responseQueue.front();
        responseQueue.pop();

        if (res == nullptr)
        {
            continue;
        }

        LOCK_ALE;

        lua_State* L = ALE::GALE->L;

        // Get function
        lua_rawgeti(L, LUA_REGISTRYINDEX, res->funcRef);

        // Push parameters
        ALE::Push(L, res->statusCode);
        ALE::Push(L, res->body);
        lua_newtable(L);
        for (const auto& item : res->headers) {
            ALE::Push(L, item.first);
            ALE::Push(L, item.second);
            lua_settable(L, -3);
        }

        // Call function
        ALE::GALE->ExecuteCall(3, 0);

        luaL_unref(L, LUA_REGISTRYINDEX, res->funcRef);

        delete res;
    }
}
