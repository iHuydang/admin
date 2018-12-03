#include <server/handlers/http_handler_base.hpp>

#include <boost/algorithm/string/split.hpp>

#include <components/statistics_storage.hpp>
#include <formats/json/serialize.hpp>
#include <formats/json/value_builder.hpp>
#include <logging/log.hpp>
#include <server/component.hpp>
#include <server/handlers/http_handler_base_statistics.hpp>
#include <server/http/http_error.hpp>
#include <server/http/http_method.hpp>
#include <server/http/http_request_impl.hpp>
#include <tracing/span.hpp>
#include <tracing/tracing.hpp>
#include <utils/graphite.hpp>
#include <utils/statistics/percentile_format_json.hpp>

namespace server {
namespace handlers {
namespace {

const std::string kXYaRequestId = "X-YaRequestId";

template <typename HeadersHolder>
std::string GetHeadersLogString(const HeadersHolder& headers_holder) {
  formats::json::ValueBuilder json_headers(formats::json::Type::kObject);
  for (const auto& header_name : headers_holder.GetHeaderNames()) {
    json_headers[header_name] = headers_holder.GetHeader(header_name);
  }
  return formats::json::ToString(json_headers.ExtractValue());
}

std::vector<http::HttpMethod> InitAllowedMethods(const HandlerConfig& config) {
  std::vector<http::HttpMethod> allowed_methods;
  auto& method_list = config.method;

  if (method_list) {
    std::vector<std::string> methods;
    boost::split(methods, *method_list, [](char c) { return c == ','; });
    for (const auto& method_str : methods) {
      auto method = http::HttpMethodFromString(method_str);
      if (!http::IsHandlerMethod(method)) {
        throw std::runtime_error(method_str +
                                 " is not supported in method list");
      }
      allowed_methods.push_back(method);
    }
  } else {
    for (auto method : http::kHandlerMethods) {
      allowed_methods.push_back(method);
    }
  }
  return allowed_methods;
}

}  // namespace

formats::json::ValueBuilder HttpHandlerBase::StatisticsToJson(
    const HttpHandlerBase::Statistics& stats) {
  formats::json::ValueBuilder result;
  formats::json::ValueBuilder total;

  formats::json::ValueBuilder reply_codes;
  size_t sum = 0;
  for (auto it : stats.GetReplyCodes()) {
    reply_codes[std::to_string(it.first)] = it.second;
    sum += it.second;
  }
  total["reply-codes"] = std::move(reply_codes);
  total["in-flight"] = stats.GetInFlight();

  total["timings"]["1min"] =
      utils::statistics::PercentileToJson(stats.GetTimings());

  result["total"] = std::move(total);
  return result;
}

HttpHandlerBase::HttpHandlerBase(
    const components::ComponentConfig& config,
    const components::ComponentContext& component_context, bool is_monitor)
    : HandlerBase(config, component_context, is_monitor),
      http_server_settings_(
          component_context
              .FindComponent<components::HttpServerSettingsBase>()),
      allowed_methods_(InitAllowedMethods(GetConfig())),
      statistics_storage_(
          component_context.FindComponent<components::StatisticsStorage>()),
      statistics_(std::make_unique<HandlerStatistics>()) {
  if (allowed_methods_.empty()) {
    LOG_WARNING() << "empty allowed methods list in " << config.Name();
  }

  if (IsEnabled()) {
    auto& server_component =
        component_context.FindComponent<components::Server>();

    engine::TaskProcessor* task_processor =
        component_context.GetTaskProcessor(GetConfig().task_processor);
    if (task_processor == nullptr) {
      throw std::runtime_error("can't find task_processor with name '" +
                               GetConfig().task_processor + '\'');
    }
    try {
      server_component.AddHandler(*this, *task_processor);
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("can't add handler to server: ") +
                               ex.what());
    }
    const auto graphite_path = "http.by-path." +
                               utils::graphite::EscapeName(GetConfig().path) +
                               ".by-handler." + config.Name();
    statistics_holder_ = statistics_storage_.GetStorage().RegisterExtender(
        graphite_path, std::bind(&HttpHandlerBase::ExtendStatistics, this,
                                 std::placeholders::_1));
  }
}

HttpHandlerBase::~HttpHandlerBase() { statistics_holder_.Unregister(); }

void HttpHandlerBase::HandleRequest(const request::RequestBase& request,
                                    request::RequestContext& context) const {
  try {
    const auto& http_request_impl =
        dynamic_cast<const http::HttpRequestImpl&>(request);
    const http::HttpRequest http_request(http_request_impl);
    auto& response = http_request.GetHttpResponse();
    const auto start_time = std::chrono::system_clock::now();

    bool log_request = http_server_settings_.NeedLogRequest();
    bool log_request_headers = http_server_settings_.NeedLogRequestHeaders();

    auto span = tracing::Span::CurrentSpan();
    assert(span);

    const auto& parent_link = http_request.GetHeader(kXYaRequestId);
    if (!parent_link.empty()) span->AddTag("parent_link", parent_link);

    span->AddTag("request_url", http_request.GetUrl());

    if (log_request) {
      logging::LogExtra log_extra;

      if (log_request_headers) {
        log_extra.Extend("request_headers", GetHeadersLogString(http_request));
      }
      log_extra.Extend("request_body", http_request.RequestBody());
      LOG_INFO() << "start handling" << std::move(log_extra);
    }

    HttpHandlerStatisticsScope stats_scope(*statistics_,
                                           http_request.GetMethod());

    try {
      response.SetData(HandleRequestThrow(http_request, context));
    } catch (const http::HttpException& ex) {
      LOG_ERROR() << "http exception in '" << HandlerName()
                  << "' handler in handle_request: code="
                  << HttpStatusString(ex.GetStatus()) << ", msg=" << ex.what()
                  << ", body=" << ex.GetExternalErrorBody();
      response.SetStatus(ex.GetStatus());
      response.SetData(ex.GetExternalErrorBody());
    } catch (const std::exception& ex) {
      LOG_ERROR() << "exception in '" << HandlerName()
                  << "' handler in handle_request: " << ex.what();
      http_request_impl.MarkAsInternalServerError();
    }

    response.SetHeader(kXYaRequestId, span->GetLink());
    span->AddTag("response_code", static_cast<int>(response.GetStatus()));

    if (log_request) {
      logging::LogExtra log_extra;

      if (log_request_headers) {
        log_extra.Extend("response_headers", GetHeadersLogString(response));
      }
      log_extra.Extend("response_data", response.GetData());
      LOG_INFO() << "finish handling " << http_request.GetUrl() << log_extra;
    }

    const auto finish_time = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        finish_time - start_time);
    stats_scope.Account(static_cast<int>(response.GetStatus()), ms);
  } catch (const std::exception& ex) {
    LOG_ERROR() << "unable to handle request: " << ex.what();
  }
}

void HttpHandlerBase::OnRequestComplete(
    const request::RequestBase& request,
    request::RequestContext& context) const {
  try {
    const http::HttpRequest http_request(
        dynamic_cast<const http::HttpRequestImpl&>(request));
    try {
      OnRequestCompleteThrow(http_request, context);
    } catch (const std::exception& ex) {
      LOG_ERROR() << "exception in '" << HandlerName()
                  << "' hander in on_request_complete: " << ex.what();
    }
  } catch (const std::exception& ex) {
    LOG_ERROR() << "unable to complete request: " << ex.what();
  }
}

const std::vector<http::HttpMethod>& HttpHandlerBase::GetAllowedMethods()
    const {
  return allowed_methods_;
}

formats::json::ValueBuilder HttpHandlerBase::ExtendStatistics(
    const utils::statistics::StatisticsRequest& /*request*/) {
  formats::json::ValueBuilder result;
  result["all-methods"] = StatisticsToJson(statistics_->GetTotalStatistics());

  if (IsMethodStatisticIncluded()) {
    formats::json::ValueBuilder by_method;
    for (auto method : GetAllowedMethods()) {
      by_method[ToString(method)] =
          StatisticsToJson(statistics_->GetStatisticByMethod(method));
    }
    result["by-method"] = std::move(by_method);
  }
  return result;
}

}  // namespace handlers
}  // namespace server
