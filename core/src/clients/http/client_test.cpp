#include <clients/http/client.hpp>

#include <set>

#include <fmt/format.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <crypto/certificate.hpp>
#include <crypto/private_key.hpp>
#include <engine/async.hpp>
#include <engine/sleep.hpp>
#include <engine/task/task_context.hpp>
#include <http/common_headers.hpp>
#include <logging/log.hpp>
#include <utils/async.hpp>
#include <utils/userver_info.hpp>

#include <utest/http_client.hpp>
#include <utest/simple_server.hpp>
#include <utest/utest.hpp>

namespace {

constexpr auto kTimeout = std::chrono::milliseconds{100};

constexpr char kTestData[] = "Test Data";
constexpr unsigned kRepetitions = 200;

constexpr char kTestHeader[] = "X-Test-Header";
constexpr char kTestHeaderMixedCase[] = "x-TEST-headeR";

constexpr char kTestUserAgent[] = "correct/2.0 (user agent) taxi_userver/000f";

constexpr char kResponse200WithHeaderPattern[] =
    "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n{}\r\n\r\n";

// Certifiacte for testing was generated via the following command:
//   `openssl req -x509 -sha256 -nodes -newkey rsa:1024 -keyout priv.key -out
//   cert.crt`
constexpr const char* kPrivateKey =
    R"(-----BEGIN PRIVATE KEY-----
MIICdgIBADANBgkqhkiG9w0BAQEFAASCAmAwggJcAgEAAoGBAKTQ8X1VKk8h83n3
eJNoZbXca1qMlFxs3fAcJJmRV/ceFvd9esM8KOzXCemcKZNVi1tyUPt+LXk/Po1i
3COTlU/+EUHO+ISgImtVdjjcE9+hLiPFINcnID2rWWNJ1pyRjqV26fj6oQMCyAW+
7ZdQivH/XtPGNwlGudsZrvxu44VvAgMBAAECgYA4uPxTkSr1fw7HjCcAPG68zzZX
PIiW4pTjXRwvifkHQGDRHmtQo/TFxiBQOQGKBmfmugoq87r8voptqHdw+wropj4Z
qdekZAWXhm8u7kYRG2Q7ZTEgRwQGCeau0hCQ5j+DU3oTM2HttEv1/CsousJrePqw
0Th/LZMUskPKGBREUQJBANmLCm2zfc9GtuQ3wqzhbpDRh3NilSJOwUK3+WOR/UfW
4Yx7Tpr5ZZr8j9Ah+kWB64p77rffErRrEZjH89jLW+kCQQDB87vemsYCz1alOBcT
xn+e7PlfmH2yGIlcJg2mNyZvVqjEPwh4ubqBHtier2jm6AoVhX9lEM4nOoY0i5f2
H3eXAkEA16asvNjtA7f+/7eDBawn1enP04NLgYn+rSwBTkJfiYKrbn6iCqDmp0Bt
NA8qsRK8szhuCdpaCX4GIKU+xo+5WQJACJ+vwMwc9c8GST5fOE/hKM3coLWFEUAq
C2DdxoA5Q0YVJvSuib+oXUlj1Fp0TaAPorlW2sWOhQwDH579WMI5bQJACCDhAqpU
BP99plWnEh4Z1EtTw3Byikn1h0exRvGtO2rnlRXVRzLnsXBX/pn7xyAHP5jPTDFN
+LfCitjxvZmWsQ==
-----END PRIVATE KEY-----)";

constexpr const char* kCertificate =
    R"(-----BEGIN CERTIFICATE-----
MIICgDCCAemgAwIBAgIJANin/30HHMYLMA0GCSqGSIb3DQEBCwUAMFkxCzAJBgNV
BAYTAlJVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX
aWRnaXRzIFB0eSBMdGQxEjAQBgNVBAMMCWxvY2FsaG9zdDAeFw0xOTEyMTIwODIx
MjhaFw0zMzA4MjAwODIxMjhaMFkxCzAJBgNVBAYTAlJVMRMwEQYDVQQIDApTb21l
LVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQxEjAQBgNV
BAMMCWxvY2FsaG9zdDCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEApNDxfVUq
TyHzefd4k2hltdxrWoyUXGzd8BwkmZFX9x4W9316wzwo7NcJ6Zwpk1WLW3JQ+34t
eT8+jWLcI5OVT/4RQc74hKAia1V2ONwT36EuI8Ug1ycgPatZY0nWnJGOpXbp+Pqh
AwLIBb7tl1CK8f9e08Y3CUa52xmu/G7jhW8CAwEAAaNQME4wHQYDVR0OBBYEFFmN
gh59kCf1PClm3I30jR9/mQO6MB8GA1UdIwQYMBaAFFmNgh59kCf1PClm3I30jR9/
mQO6MAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQELBQADgYEAAGt9Vo5bM3WHTLza
Jd+x3JbeCAMqz831yCAp2kpssNa0rRNfC3QX3GEKWGMjxgUKpS/9V8tHH/K3jI+K
57DUESC0/NBo4r76JIjMga4i7W7Eh5XD1jnPdfvSGumBIks2UMJV7FaZHwUjr4fP
g3n5Bom64kOrAWOk2xcpd0Pm00o=
-----END CERTIFICATE-----)";

constexpr char kResponse301WithHeaderPattern[] =
    "HTTP/1.1 301 OK\r\nConnection: close\r\nContent-Length: 0\r\n{}\r\n\r\n";

class RequestMethodTestData final {
 public:
  using Request = clients::http::Request;
  using TwoArgsFunction = std::shared_ptr<Request> (Request::*)(
      const std::string& url, std::string data_);
  using OneArgFunction =
      std::shared_ptr<Request> (Request::*)(const std::string& url);

  RequestMethodTestData(const char* method_name, const char* data,
                        TwoArgsFunction func)
      : method_name_(method_name), data_(data), func_two_args_(func) {}

  RequestMethodTestData(const char* method_name, const char* data,
                        OneArgFunction func)
      : method_name_(method_name), data_(data), func_one_arg_(func) {}

  template <class Callback>
  bool PerformRequest(const std::string& url, const Callback& callback,
                      clients::http::Client& client) const {
    *callback.method_name = method_name_;
    *callback.data = data_;

    auto request = client.CreateRequest();
    if (func_two_args_) {
      request = (request.get()->*func_two_args_)(url, data_);
    } else {
      request = (request.get()->*func_one_arg_)(url);
    }

    return request->verify(true)
        ->http_version(clients::http::HttpVersion::k11)
        ->timeout(kTimeout)
        ->perform()
        ->IsOk();
  }

  const char* GetMethodName() const { return method_name_; }

 private:
  const char* method_name_;
  const char* data_;
  TwoArgsFunction func_two_args_{nullptr};
  OneArgFunction func_one_arg_{nullptr};
};

using HttpResponse = testing::SimpleServer::Response;
using HttpRequest = testing::SimpleServer::Request;
using HttpCallback = testing::SimpleServer::OnRequest;

static std::optional<HttpResponse> process_100(const HttpRequest& request) {
  const bool requires_continue =
      (request.find("Expect: 100-continue") != std::string::npos);

  if (requires_continue || request.empty()) {
    return HttpResponse{
        "HTTP/1.1 100 Continue\r\nContent-Length: "
        "0\r\n\r\n",
        HttpResponse::kWriteAndContinue};
  }

  return std::nullopt;
}

static HttpResponse echo_callback(const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;

  const auto cont = process_100(request);
  if (cont) {
    return *cont;
  }

  const auto data_pos = request.find("\r\n\r\n");
  std::string payload;
  if (data_pos == std::string::npos) {
    // We have no headers after 100 Continue
    payload = request;
  } else {
    payload = request.substr(data_pos + 4);
  }
  LOG_INFO() << "HTTP Server sending payload: " << payload;

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
      "text/html\r\nContent-Length: " +
          std::to_string(payload.size()) + "\r\n\r\n" + payload,
      HttpResponse::kWriteAndClose};
}

struct validating_shared_callback {
  const std::shared_ptr<std::string> method_name =
      std::make_shared<std::string>();

  const std::shared_ptr<std::string> data = std::make_shared<std::string>();

  HttpResponse operator()(const HttpRequest& request) const {
    LOG_INFO() << "HTTP Server receive: " << request;

    const auto cont = process_100(request);

    EXPECT_FALSE(!!cont) << "This callback does not work with CONTINUE";

    const auto first_line_end = request.find("\r\n");
    EXPECT_LT(request.find(*method_name), first_line_end)
        << "No '" << *method_name
        << "' in first line of a request: " << request;

    const auto header_end = request.find("\r\n\r\n", first_line_end) + 4;
    EXPECT_EQ(request.substr(header_end), *data)
        << "Request body differ from '" << *data
        << "'. Whole request: " << request;

    return {
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
        "text/html\r\nContent-Length: " +
            std::to_string(request.size()) + "\r\n\r\n" + request,
        HttpResponse::kWriteAndClose};
  }
};

static HttpResponse put_validate_callback(const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;

  EXPECT_NE(request.find("PUT"), std::string::npos)
      << "PUT request has no PUT in headers: " << request;

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
      "0\r\n\r\n",
      HttpResponse::kWriteAndClose};
}

static HttpResponse sleep_callback_base(const HttpRequest& request,
                                        std::chrono::milliseconds sleep_for) {
  LOG_INFO() << "HTTP Server receive: " << request;

  engine::InterruptibleSleepFor(sleep_for);

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
      "4096\r\n\r\n" +
          std::string(4096, '@'),
      HttpResponse::kWriteAndClose};
}

static HttpResponse sleep_callback(const HttpRequest& request) {
  return sleep_callback_base(request, kMaxTestWaitTime);
}

static HttpResponse sleep_callback_1s(const HttpRequest& request) {
  return sleep_callback_base(request, std::chrono::seconds(1));
}

static HttpResponse huge_data_callback(const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;

  const auto cont = process_100(request);
  if (cont) {
    return *cont;
  }

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
      "text/html\r\nContent-Length: "
      "100000\r\n\r\n" +
          std::string(100000, '@'),
      HttpResponse::kWriteAndClose};
}

std::string TryGetHeader(const HttpRequest& request, std::string_view header) {
  const auto first_pos = request.find(header);
  if (first_pos == std::string::npos) return {};
  const auto second_pos = request.find(header, first_pos + header.length());
  EXPECT_EQ(second_pos, std::string::npos)
      << "Header `" << header
      << "` exists more than once in request: " << request;

  auto values_begin_pos = request.find(":", first_pos + header.length()) + 1;
  auto values_end_pos = request.find("\r", values_begin_pos);

  std::string header_value(request.data() + values_begin_pos,
                           values_end_pos - values_begin_pos);
  boost::trim(header_value);
  return header_value;
}

std::string AssertHeader(const HttpRequest& request, std::string_view header) {
  const auto first_pos = request.find(header);
  EXPECT_NE(first_pos, std::string::npos)
      << "Failed to find header `" << header << "` in request: " << request;

  return TryGetHeader(request, header);
}

static HttpResponse header_validate_callback(const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;
  AssertHeader(request, kTestHeader);
  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
      "0\r\n\r\n",
      HttpResponse::kWriteAndClose};
}

static HttpResponse user_agent_validate_callback(const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;
  auto header_value = AssertHeader(request, http::headers::kUserAgent);

  EXPECT_EQ(header_value, kTestUserAgent) << "In request: " << request;

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
      "0\r\n\r\n",
      HttpResponse::kWriteAndClose};
}

static HttpResponse no_user_agent_validate_callback(
    const HttpRequest& request) {
  LOG_INFO() << "HTTP Server receive: " << request;
  auto header_value = TryGetHeader(request, http::headers::kUserAgent);
  EXPECT_EQ(header_value, utils::GetUserverIdentifier())
      << "In request: " << request;

  return {
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
      "0\r\n\r\n",
      HttpResponse::kWriteAndClose};
}

struct Response200WithHeader {
  const std::string header;

  HttpResponse operator()(const HttpRequest&) const {
    return {
        fmt::format(kResponse200WithHeaderPattern, header),
        HttpResponse::kWriteAndClose,
    };
  }
};

struct Response301WithHeader {
  const std::string location;
  const std::string header;

  HttpResponse operator()(const HttpRequest&) const {
    return {
        fmt::format(kResponse301WithHeaderPattern,
                    "Location: " + location + "\r\n" + header),
        HttpResponse::kWriteAndClose,
    };
  }
};

struct Response503WithConnDrop {
  HttpResponse operator()(const HttpRequest&) const {
    return {
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Length: 0\r\n"
        "\r\n ",
        HttpResponse::kWriteAndClose};
  }
};

struct CheckCookie {
  const std::set<std::string> expected_cookies;

  HttpResponse operator()(const HttpRequest& request) {
    const auto header_pos = request.find("Cookie:");
    EXPECT_NE(header_pos, std::string::npos)
        << "Failed to find 'Cookie' header in request: " << request;

    EXPECT_EQ(request.find("Cookie:", header_pos + 1), std::string::npos)
        << "Duplicate 'Cookie' header in request: " << request;

    const auto value_start = request.find_first_not_of(' ', header_pos + 7);
    EXPECT_NE(value_start, std::string::npos)
        << "Malformed request: " << request;
    const auto value_end = request.find("\r\n", value_start);
    EXPECT_NE(value_end, std::string::npos) << "Malformed request: " << request;

    auto value = request.substr(value_start, value_end - value_start);
    std::vector<std::string> received_cookies;
    boost::split(received_cookies, value, [](char c) { return c == ';'; });

    auto unseen_cookies = expected_cookies;
    for (auto cookie : received_cookies) {
      boost::trim(cookie);
      EXPECT_TRUE(unseen_cookies.erase(cookie))
          << "Unexpected cookie '" << cookie << "' in request: " << request;
    }
    EXPECT_TRUE(unseen_cookies.empty()) << "Not all cookies received";

    return {"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            HttpResponse::kWriteAndClose};
  }
};

}  // namespace

TEST(HttpClient, PostEcho) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&echo_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    const auto res = http_client_ptr->CreateRequest()
                         ->post(http_server.GetBaseUrl(), kTestData)
                         ->retry(1)
                         ->verify(true)
                         ->http_version(clients::http::HttpVersion::k11)
                         ->timeout(kTimeout)
                         ->perform();

    EXPECT_EQ(res->body(), kTestData);

    const auto stats = res->GetStats();
    EXPECT_EQ(stats.retries_count, 0);
    EXPECT_GE(stats.open_socket_count, 1);
    EXPECT_GT(stats.time_to_process, std::chrono::seconds(0));
    EXPECT_GT(stats.time_to_connect, std::chrono::seconds(0));

    EXPECT_LT(stats.time_to_process, kTimeout);
    EXPECT_LT(stats.time_to_connect, kTimeout);
  });
}

TEST(HttpClient, StatsOnTimeout) {
  TestInCoro([] {
    const int kRetries = 5;
    const testing::SimpleServer http_server{&sleep_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    try {
      const auto res = http_client_ptr->CreateRequest()
                           ->post(http_server.GetBaseUrl(), kTestData)
                           ->retry(kRetries)
                           ->verify(true)
                           ->http_version(clients::http::HttpVersion::k11)
                           ->timeout(kTimeout)
                           ->perform();
    } catch (const clients::http::BaseException& e) {
      EXPECT_EQ(e.GetStats().retries_count, kRetries - 1);
      EXPECT_EQ(e.GetStats().open_socket_count, kRetries);

      EXPECT_GE(e.GetStats().time_to_process, kTimeout);
      EXPECT_LT(e.GetStats().time_to_process, kTimeout * kRetries);
    }
  });
}

TEST(HttpClient, CancelPre) {
  TestInCoro([] {
    auto task = utils::Async("test", [] {
      const testing::SimpleServer http_server{&echo_callback};
      auto http_client_ptr = utest::CreateHttpClient();

      engine::current_task::GetCurrentTaskContext()->RequestCancel(
          engine::TaskCancellationReason::kUserRequest);

      EXPECT_THROW(http_client_ptr->CreateRequest(),
                   clients::http::CancelException);
    });

    task.Get();
  });
}

TEST(HttpClient, CancelPost) {
  TestInCoro([] {
    auto task = utils::Async("test", [] {
      const testing::SimpleServer http_server{&echo_callback};
      auto http_client_ptr = utest::CreateHttpClient();

      const auto request = http_client_ptr->CreateRequest()
                               ->post(http_server.GetBaseUrl(), kTestData)
                               ->timeout(kTimeout);

      engine::current_task::GetCurrentTaskContext()->RequestCancel(
          engine::TaskCancellationReason::kUserRequest);

      auto future = request->async_perform();
      EXPECT_THROW(future.Wait(), clients::http::CancelException);
    });

    task.Get();
  });
}

TEST(HttpClient, CancelRetries) {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;

  std::atomic<unsigned> server_requests{0};
  unsigned client_retries = 0;

  TestInCoro([&server_requests, &client_retries] {
    constexpr unsigned kRetriesCount = 100;
    constexpr unsigned kMinRetries = 3;
    constexpr auto kMaxNonIoReactionTime = std::chrono::seconds{1};
    auto callback = [&server_requests](const HttpRequest& request) {
      ++server_requests;
      return sleep_callback_1s(request);
    };

    const testing::SimpleServer http_server{callback};
    auto http_client_ptr = utest::CreateHttpClient();

    const auto start_create_request_time = std::chrono::steady_clock::now();
    auto future = std::make_unique<clients::http::ResponseFuture>(
        http_client_ptr->CreateRequest()
            ->post(http_server.GetBaseUrl(), kTestData)
            ->retry(kRetriesCount)
            ->verify(true)
            ->http_version(clients::http::HttpVersion::k11)
            ->timeout(kTimeout)
            ->async_perform());

    engine::SleepFor(kTimeout * (kMinRetries + 1));

    const auto cancelation_start_time = std::chrono::steady_clock::now();
    engine::current_task::GetCurrentTaskContext()->RequestCancel(
        engine::TaskCancellationReason::kUserRequest);

    try {
      [[maybe_unused]] auto val = future->Wait();
      FAIL() << "Must have been canceled";
    } catch (const clients::http::CancelException& e) {
      client_retries = e.GetStats().retries_count;
      EXPECT_LE(client_retries, kMinRetries * 2);
      EXPECT_GE(client_retries, kMinRetries);
    }

    const auto cancellation_end_time = std::chrono::steady_clock::now();
    const auto cancellation_duration =
        cancellation_end_time - cancelation_start_time;
    EXPECT_LT(cancellation_duration, kTimeout * 2)
        << "Looks like cancel did not cancelled the request, because after the "
           "cancel the request has been working for "
        << duration_cast<milliseconds>(cancellation_duration).count() << "ms";

    future.reset();
    const auto future_destruction_time = std::chrono::steady_clock::now();
    const auto future_destruction_duration =
        future_destruction_time - cancellation_end_time;
    EXPECT_LT(future_destruction_duration, kMaxNonIoReactionTime)
        << "Looks like cancel did not cancelled the request, because after the "
           "cancel future has been destructing for "
        << duration_cast<milliseconds>(future_destruction_duration).count()
        << "ms";

    const auto request_creation_duration =
        cancelation_start_time - start_create_request_time;
    EXPECT_LT(request_creation_duration, kMaxNonIoReactionTime);

    EXPECT_GE(server_requests, kMinRetries);
    EXPECT_LT(server_requests, kMinRetries * 2);
  });

  EXPECT_LE(server_requests.load(), client_retries + 1)
      << "Cancel() is not fast enough and more than 1 retry was done after "
         "cancellation";
}

TEST(HttpClient, PostShutdownWithPendingRequest) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&sleep_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    for (unsigned i = 0; i < kRepetitions; ++i)
      http_client_ptr->CreateRequest()
          ->post(http_server.GetBaseUrl(), kTestData)
          ->retry(1)
          ->verify(true)
          ->http_version(clients::http::HttpVersion::k11)
          ->timeout(kTimeout)
          ->async_perform()
          .Detach();  // Do not do like this in production code!
  });
}

TEST(HttpClient, PostShutdownWithPendingRequestHuge) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&sleep_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    std::string request = kTestData;
    for (unsigned i = 0; i < 20; ++i) {
      request += request;
    }

    for (unsigned i = 0; i < kRepetitions; ++i)
      http_client_ptr->CreateRequest()
          ->post(http_server.GetBaseUrl(), request)
          ->retry(1)
          ->verify(true)
          ->http_version(clients::http::HttpVersion::k11)
          ->timeout(kTimeout)
          ->async_perform()
          .Detach();  // Do not do like this in production code!
  });
}

TEST(HttpClient, PutEcho) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&echo_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    const auto res = http_client_ptr->CreateRequest()
                         ->put(http_server.GetBaseUrl(), kTestData)
                         ->retry(1)
                         ->verify(true)
                         ->http_version(clients::http::HttpVersion::k11)
                         ->timeout(kTimeout)
                         ->perform();

    EXPECT_EQ(res->body(), kTestData);
  });
}

TEST(HttpClient, PutValidateHeader) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&put_validate_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    const auto res = http_client_ptr->CreateRequest()
                         ->put(http_server.GetBaseUrl(), kTestData)
                         ->retry(1)
                         ->verify(true)
                         ->http_version(clients::http::HttpVersion::k11)
                         ->timeout(kTimeout)
                         ->perform();

    EXPECT_TRUE(res->IsOk());
  });
}

TEST(HttpClient, PutShutdownWithPendingRequest) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&sleep_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    for (unsigned i = 0; i < kRepetitions; ++i)
      http_client_ptr->CreateRequest()
          ->put(http_server.GetBaseUrl(), kTestData)
          ->retry(1)
          ->verify(true)
          ->http_version(clients::http::HttpVersion::k11)
          ->timeout(kTimeout)
          ->async_perform()
          .Detach();  // Do not do like this in production code!
  });
}

TEST(HttpClient, PutShutdownWithPendingRequestHuge) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&sleep_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    std::string request = kTestData;
    for (unsigned i = 0; i < 20; ++i) {
      request += request;
    }

    for (unsigned i = 0; i < kRepetitions; ++i)
      http_client_ptr->CreateRequest()
          ->put(http_server.GetBaseUrl(), request)
          ->retry(1)
          ->verify(true)
          ->http_version(clients::http::HttpVersion::k11)
          ->timeout(kTimeout)
          ->async_perform()
          .Detach();  // Do not do like this in production code!
  });
}

TEST(HttpClient, PutShutdownWithHugeResponse) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&huge_data_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    for (unsigned i = 0; i < kRepetitions; ++i)
      http_client_ptr->CreateRequest()
          ->put(http_server.GetBaseUrl(), kTestData)
          ->retry(1)
          ->verify(true)
          ->http_version(clients::http::HttpVersion::k11)
          ->timeout(kTimeout)
          ->async_perform()
          .Detach();  // Do not do like this in production code!
  });
}

TEST(HttpClient, MethodsMix) {
  TestInCoro([] {
    using clients::http::Request;

    const validating_shared_callback callback{};
    const testing::SimpleServer http_server{callback};
    const auto http_client = utest::CreateHttpClient();

    const RequestMethodTestData tests[] = {
        {"PUT", kTestData, &Request::put},
        {"POST", kTestData, &Request::post},
        {"GET", "", &Request::get},
        {"HEAD", "", &Request::head},
        {"DELETE", "", &Request::delete_method},
        {"PATCH", kTestData, &Request::patch},
    };

    for (const auto& method1 : tests) {
      for (const auto& method2 : tests) {
        const bool ok1 = method1.PerformRequest(http_server.GetBaseUrl(),
                                                callback, *http_client);
        EXPECT_TRUE(ok1) << "Failed to perform " << method1.GetMethodName();

        const auto ok2 = method2.PerformRequest(http_server.GetBaseUrl(),
                                                callback, *http_client);
        EXPECT_TRUE(ok2) << "Failed to perform " << method2.GetMethodName()
                         << " after " << method1.GetMethodName();
      }
    }
  });
}

TEST(HttpClient, Headers) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&header_validate_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    clients::http::Headers headers;
    headers.emplace(kTestHeader, "test");
    headers.emplace(kTestHeaderMixedCase, "notest");  // should be ignored

    for (unsigned i = 0; i < kRepetitions; ++i) {
      const auto response = http_client_ptr->CreateRequest()
                                ->post(http_server.GetBaseUrl(), kTestData)
                                ->retry(1)
                                ->headers(headers)
                                ->verify(true)
                                ->http_version(clients::http::HttpVersion::k11)
                                ->timeout(kTimeout)
                                ->perform();

      EXPECT_TRUE(response->IsOk());
    }
  });
}

TEST(HttpClient, HeadersUserAgent) {
  TestInCoro([] {
    const testing::SimpleServer http_server{&user_agent_validate_callback};
    const testing::SimpleServer http_server_no_ua{
        &no_user_agent_validate_callback};
    auto http_client_ptr = utest::CreateHttpClient();

    auto response = http_client_ptr->CreateRequest()
                        ->post(http_server.GetBaseUrl(), kTestData)
                        ->retry(1)
                        ->headers({{http::headers::kUserAgent, kTestUserAgent}})
                        ->verify(true)
                        ->http_version(clients::http::HttpVersion::k11)
                        ->timeout(kTimeout)
                        ->perform();

    EXPECT_TRUE(response->IsOk());

    response =
        http_client_ptr->CreateRequest()
            ->post(http_server.GetBaseUrl(), kTestData)
            ->retry(1)
            ->verify(true)
            ->http_version(clients::http::HttpVersion::k11)
            ->timeout(kTimeout)
            ->headers({{http::headers::kUserAgent, "Header to override"}})
            ->headers({{http::headers::kUserAgent, kTestUserAgent}})
            ->perform();

    EXPECT_TRUE(response->IsOk());

    response = http_client_ptr->CreateRequest()
                   ->post(http_server_no_ua.GetBaseUrl(), kTestData)
                   ->retry(1)
                   ->verify(true)
                   ->http_version(clients::http::HttpVersion::k11)
                   ->timeout(kTimeout)
                   ->perform();
    EXPECT_TRUE(response->IsOk());
  });
}

TEST(HttpClient, Cookies) {
  TestInCoro([] {
    const auto test = [](const clients::http::Request::Cookies& cookies,
                         std::set<std::string> expected) {
      const testing::SimpleServer http_server{CheckCookie{std::move(expected)}};
      auto http_client_ptr = utest::CreateHttpClient();
      for (unsigned i = 0; i < kRepetitions; ++i) {
        const auto response =
            http_client_ptr->CreateRequest()
                ->get(http_server.GetBaseUrl())
                ->retry(1)
                ->cookies(cookies)
                ->verify(true)
                ->http_version(clients::http::HttpVersion::k11)
                ->timeout(kTimeout)
                ->perform();
        EXPECT_TRUE(response->IsOk());
      }
    };
    test({{"a", "b"}}, {"a=b"});
    test({{"A", "B"}}, {"A=B"});
    test({{"a", "B"}, {"A", "b"}}, {"a=B", "A=b"});
  });
}

TEST(HttpClient, HeadersAndWhitespaces) {
  TestInCoro([] {
    auto http_client_ptr = utest::CreateHttpClient();

    const std::string header_data = kTestData;
    const std::string header_values[] = {
        header_data,
        "     " + header_data,
        "\t \t" + header_data,
        "\t \t" + header_data + "   \t",
        "\t \t" + header_data + "\t ",
        header_data + "   \t",
        header_data + "\t ",
    };

    for (const auto& header_value : header_values) {
      const testing::SimpleServer http_server{
          Response200WithHeader{std::string(kTestHeader) + ':' + header_value}};

      const auto response = http_client_ptr->CreateRequest()
                                ->post(http_server.GetBaseUrl())
                                ->timeout(kTimeout)
                                ->perform();

      EXPECT_TRUE(response->IsOk())
          << "Header value is '" << header_value << "'";
      ASSERT_TRUE(response->headers().count(kTestHeader))
          << "Header value is '" << header_value << "'";
      ASSERT_TRUE(response->headers().count(kTestHeaderMixedCase))
          << "Header value is '" << header_value << "'";
      EXPECT_EQ(response->headers()[kTestHeader], header_data)
          << "Header value is '" << header_value << "'";
      EXPECT_EQ(response->headers()[kTestHeaderMixedCase], header_data)
          << "Header value is '" << header_value << "'";
    }
  });
}

// Make sure that certs are setuped and reset on the end of a request.
//
// Smoke test. Fails on MacOS with Segmentation fault while calling
// Request::RequestImpl::on_certificate_request, probably because CURL library
// was misconfigured and uses wrong version of OpenSSL.
TEST(HttpClient, DISABLED_IN_MAC_OS_TEST_NAME(HttpsWithCert)) {
  TestInCoro([] {
    auto pkey = crypto::PrivateKey::LoadFromString(kPrivateKey, "");
    auto cert = crypto::Certificate::LoadFromString(kCertificate);
    auto http_client_ptr = utest::CreateHttpClient();
    const testing::SimpleServer http_server{echo_callback};
    const auto url = http_server.GetBaseUrl();
    const auto ssl_url =
        http_server.GetBaseUrl(testing::SimpleServer::Schema::kHttps);

    // SSL is slow, setting big timeout to avoid test flapping
    const auto kTimeout = std::chrono::seconds(1);

    // Running twice to make sure that after request without a cert the request
    // with a cert succeeds and do not break other request types.
    for (unsigned i = 0; i < 2; ++i) {
      auto response_future = http_client_ptr->CreateRequest()
                                 ->post(ssl_url)
                                 ->timeout(kTimeout)
                                 ->client_key_cert(pkey, cert)
                                 ->async_perform();

      response_future.Wait();
      EXPECT_THROW(response_future.Get(), std::exception)
          << "SSL is not used by the server but the request with private key "
             "succeeded";

      auto response = http_client_ptr->CreateRequest()
                          ->post(url)
                          ->timeout(kTimeout)
                          ->client_key_cert(pkey, cert)
                          ->perform();

      EXPECT_TRUE(response->IsOk());

      response = http_client_ptr->CreateRequest()
                     ->post(url)
                     ->timeout(kTimeout)
                     ->perform();

      EXPECT_TRUE(response->IsOk());
    }
  });
}

TEST(HttpClient, RedirectHeaders) {
  TestInCoro([] {
    auto http_client_ptr = utest::CreateHttpClient();

    const testing::SimpleServer http_server_final{
        Response200WithHeader{"xxx: good"}};

    const testing::SimpleServer http_server_redirect{
        Response301WithHeader{http_server_final.GetBaseUrl(), "xxx: bad"}};

    const auto response = http_client_ptr->CreateRequest()
                              ->post(http_server_redirect.GetBaseUrl())
                              ->timeout(std::chrono::milliseconds(1000))
                              ->perform();

    EXPECT_TRUE(response->IsOk());
    EXPECT_EQ(response->headers()["xxx"], "good");
    EXPECT_EQ(response->headers()["XXX"], "good");
  });
}

TEST(HttpClient, BadUrl) {
  TestInCoro([] {
    auto http_client_ptr = utest::CreateHttpClient();
    auto request = http_client_ptr->CreateRequest();

    const auto check = [&](const std::string& url) {
      [[maybe_unused]] auto response = request->url(url)->perform();
      // recreate request if reached here
      request = http_client_ptr->CreateRequest();
    };

    EXPECT_THROW(check({}), clients::http::BadArgumentException);
    EXPECT_THROW(check("http://"), clients::http::BadArgumentException);
    EXPECT_THROW(check("http:\\\\localhost/"),
                 clients::http::BadArgumentException);
    EXPECT_THROW(check("http:///?query"), clients::http::BadArgumentException);
    // three slashes before hostname are apparently okay
    EXPECT_THROW(check("http:////path/"), clients::http::BadArgumentException);
    // we allow no-scheme URLs for now
    // EXPECT_THROW(check("localhost/"), clients::http::BadArgumentException);
    // EXPECT_THROW(check("ftp.localhost/"),
    // clients::http::BadArgumentException);
    EXPECT_THROW(check("http://localhost:99999/"),
                 clients::http::BadArgumentException);
    EXPECT_THROW(check("http://localhost:abcd/"),
                 clients::http::BadArgumentException);
  });
}

TEST(HttpClient, Retry) {
  TestInCoro([] {
    auto http_client_ptr = utest::CreateHttpClient();
    const testing::SimpleServer unavail_server{Response503WithConnDrop{}};

    auto response = http_client_ptr->CreateRequest()
                        ->get(unavail_server.GetBaseUrl())
                        ->timeout(kTimeout)
                        ->retry(3)
                        ->perform();

    EXPECT_FALSE(response->IsOk());
    EXPECT_EQ(503, response->status_code());
    EXPECT_EQ(2, response->GetStats().retries_count);
  });
}

TEST(HttpClient, TinyTimeout) {
  TestInCoro([] {
    auto http_client_ptr = utest::CreateHttpClient();
    const testing::SimpleServer http_server{sleep_callback_1s};

    for (unsigned i = 0; i < kRepetitions; ++i) {
      auto response_future = http_client_ptr->CreateRequest()
                                 ->post(http_server.GetBaseUrl(), kTestData)
                                 ->retry(1)
                                 ->verify(true)
                                 ->http_version(clients::http::HttpVersion::k11)
                                 ->timeout(std::chrono::milliseconds(1))
                                 ->async_perform();

      response_future.Wait();
      EXPECT_THROW(response_future.Get(), std::exception);
    }
  });
}
