# Upstream proposal: configurable / longer httplib client timeouts for `HttpRequest`

> Draft text prepared for an upstream issue/PR against
> [azerothcore/mod-ale](https://github.com/azerothcore/mod-ale).
> **Not yet posted.**

## Symptom

The Lua `HttpRequest` API is unusable against any backend whose response takes
longer than 5 seconds. In `HttpManager::HttpWorkerThread()`
(`src/LuaEngine/HttpManager.cpp`), every outbound `httplib::Client` is configured
with a hard-coded 5-second read timeout:

```cpp
cli.set_read_timeout(5, 0);  // 5 seconds
cli.set_write_timeout(5, 0); // 5 seconds
```

This 5 s read timeout is below the latency floor of many legitimate backends —
LLM inference endpoints (commonly 15–25 s), slow REST APIs, or DB-heavy queries.
When the client times out:

1. `HttpManager` logs an `HTTP request error` and `continue`s, so **no response
   is ever pushed to the response queue** — the Lua callback registered with the
   request **never fires**, with no recoverable signal on the Lua side.
2. The client's mid-flight FIN/RST can tear down the server-side connection
   before it finishes processing, so the request looks "dropped" from both ends.

This makes `HttpRequest` effectively incompatible with any non-trivial backend,
and the failure mode is silent from the script author's perspective.

## Proposed fix

Raise the client read/write timeouts to values that cover realistic slow
backends while still bounding worst-case worker-thread occupation: **60 s read /
30 s write**. The 3 s `connection_timeout` is left unchanged.

This is the exact change shipped in the Chronicle fork as commit `3efab5e`
(*fix(LuaEngine/Http): bump httplib client read/write timeout 5s→60s/30s*),
applied to both the initial client (`cli`) and the redirect-follow client
(`cli2`):

```diff
--- a/src/LuaEngine/HttpManager.cpp
+++ b/src/LuaEngine/HttpManager.cpp
@@ -137,8 +137,8 @@ void HttpManager::HttpWorkerThread()
 
             httplib::Client cli(host);
             cli.set_connection_timeout(0, 3000000); // 3 seconds
-            cli.set_read_timeout(5, 0); // 5 seconds
-            cli.set_write_timeout(5, 0); // 5 seconds
+            cli.set_read_timeout(60, 0); // 60 seconds — covers LLM inference + safety margin
+            cli.set_write_timeout(30, 0); // 30 seconds
 
             httplib::Result res = DoRequest(cli, req, path);
             httplib::Error err = res.error();
@@ -161,8 +161,8 @@ void HttpManager::HttpWorkerThread()
                 }
                 httplib::Client cli2(host);
                 cli2.set_connection_timeout(0, 3000000); // 3 seconds
-                cli2.set_read_timeout(5, 0); // 5 seconds
-                cli2.set_write_timeout(5, 0); // 5 seconds
+                cli2.set_read_timeout(60, 0); // 60 seconds — covers LLM inference + safety margin
+                cli2.set_write_timeout(30, 0); // 30 seconds
                 res = DoRequest(cli2, req, path);
             }
```

`60 s read + 30 s write` covers typical LLM inference latency (15–25 s) with a
safety margin, while still bounding how long a worker thread can be occupied by a
single stalled request.

## Ideal alternative: per-request configurable timeout

Bumping the hard-coded constants fixes the immediate problem but trades one fixed
policy for another — a fast-path script still pays a 60 s worst case, and a
30 s-LLM still has no way to ask for more. The cleaner upstream solution is to
make the timeout **configurable per request** rather than compiled-in.

Suggested shape (non-breaking, default preserves current/raised behaviour):

- Extend the Lua `HttpRequest` signature with an optional `timeout_seconds`
  argument (or an options table), carried on `HttpWorkItem`.
- In `HttpWorkerThread()`, apply that value via `set_read_timeout(...)` per
  request, falling back to a sane default constant when unset.
- Optionally expose the default through the module config so server operators can
  tune it without recompiling.

This keeps the engine safe by default while letting integrators with slow
backends (LLM pipelines, batch APIs) opt into longer waits explicitly, and lets
latency-sensitive callers opt into shorter ones. The 60s/30s bump above can ship
first as the immediate fix, with the configurable API as a follow-up.
