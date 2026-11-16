#include <phosphor-logging/lg2.hpp>
#include <redfish_client/core/sensor_handler.hpp>
#include <sdbusplus/async/timer.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <limits>
#include <memory>

PHOSPHOR_LOG2_USING;

namespace redfish_client::core
{

namespace
{

double parseMetricValue(const nlohmann::json& val)
{
    if (val.is_string())
    {
        const auto& strVal = val.get_ref<const std::string&>();
        double result = 0;
        if (auto [ptr, ec] = std::from_chars(
                strVal.data(), strVal.data() + strVal.size(), result);
            ec == std::errc())
        {
            return result;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

auto loop(sdbusplus::async::context& ctx,
          std::unique_ptr<SensorHandler> handler, size_t intervalMilliseconds)
    -> sdbusplus::async::task<void>
{
    while (!ctx.stop_requested())
    {
        co_await handler->load(ctx);
        co_await sdbusplus::async::sleep_for(
            ctx, std::chrono::milliseconds(intervalMilliseconds));
    };
    co_return;
}

} // namespace

auto SensorHandler::run(sdbusplus::async::context& ctx, const std::string& host,
                        const SensorConfig& config)
    -> sdbusplus::async::task<void>
{
    ctx.spawn(loop(ctx, std::make_unique<SensorHandler>(host, config),
                   config.intervalMilliseconds));
    co_return;
}

auto SensorHandler::load(sdbusplus::async::context& ctx)
    -> sdbusplus::async::task<void>
{
    if (config.metricReportUrls)
    {
        for (const auto& reportPath : *config.metricReportUrls)
        {
            co_await fetchAndUpdateSensorsFromMetricReport(ctx, reportPath);
        }
    }

    // Individually load the sensors not (yet) driven by a report. When no
    // reports are configured, reportCovered stays empty and every sensor is
    // loaded.
    for (const auto& mapper : config.mappers)
    {
        if (reportCovered.contains(mapper.fromUrl))
        {
            continue;
        }
        auto maybeSensor = co_await fetchSensor(ctx, mapper.fromUrl);
        if (maybeSensor.has_value())
        {
            updateSensor(ctx, mapper, maybeSensor.value());
        }
    }
    co_return;
}

auto SensorHandler::fetchSensor(sdbusplus::async::context& ctx,
                                const std::string& path, bool cacheConnection)
    -> sdbusplus::async::task<std::optional<redfish_binding::Sensor::Sensor>>
{
    auto url = std::format("http://{}{}", host, path);

    // A repeatedly-polled sensor keeps its connection cached for reuse across
    // calls; a one-shot read uses a throwaway connection so nothing lingers in
    // httpHandles.
    std::unique_ptr<AsyncHttpHandle> ownHandle;
    AsyncHttpHandle* handle = nullptr;
    if (cacheConnection && config.cacheConnection)
    {
        auto& cached = httpHandles[path];
        if (!cached)
        {
            cached = std::make_unique<AsyncHttpHandle>(url);
        }
        handle = cached.get();
    }
    else
    {
        ownHandle = std::make_unique<AsyncHttpHandle>(url);
        handle = ownHandle.get();
    }

    for (size_t i = 0; i < config.maxRetries; ++i)
    {
        if (i != 0)
        {
            co_await sdbusplus::async::sleep_for(
                ctx,
                std::chrono::milliseconds(config.retryIntervalMilliseconds));
        }
        auto response = co_await handle->tryGet(ctx);
        if (!response.has_value())
        {
            info("Http error for {URL}: {ERR}", "URL", url, "ERR",
                 response.error());
            continue;
        }
        if (response->code != 200)
        {
            if (!config.ignoreUnavailableSensor)
            {
                info("Http response error code from {URL}: {CODE}", "URL",
                     url, "CODE", response->code);
            }
            continue;
        }
        auto trySensor =
            redfish_binding::Sensor::tryParseSensor(response->body);
        if (!trySensor.has_value())
        {
            info("Failed to parse sensor from {URL}: {ERR}, response: {RES}",
                 "URL", url, "ERR", trySensor.error(), "RES", response->body);
            continue;
        }
        co_return std::move(trySensor.value());
    }
    co_return std::nullopt;
}

auto SensorHandler::fetchAndUpdateSensorsFromMetricReport(
    sdbusplus::async::context& ctx, const std::string& reportPath)
    -> sdbusplus::async::task<void>
{
    auto url = std::format("http://{}{}", host, reportPath);
    auto& handle = httpHandles[reportPath];
    if (!handle)
    {
        handle = std::make_unique<AsyncHttpHandle>(url);
    }
    auto response = co_await handle->tryGet(ctx);
    if (!response.has_value())
    {
        info("Http error for {URL}: {ERR}", "URL", url, "ERR",
             response.error());
        co_return;
    }
    if (response->code != 200)
    {
        if (!config.ignoreUnavailableSensor)
        {
            info("Http response error code from {URL}: {CODE}", "URL", url,
                 "CODE", response->code);
        }
        co_return;
    }
    auto report = nlohmann::json::parse(response->body, nullptr, false);
    if (report.is_discarded())
    {
        info("Failed to parse metric report from {URL}: {RES}", "URL", url,
             "RES", response->body);
        co_return;
    }
    if (!report.contains("MetricValues") || !report["MetricValues"].is_array())
    {
        co_return;
    }

    for (const auto& metricValue : report["MetricValues"])
    {
        if (!metricValue.contains("MetricProperty") ||
            !metricValue["MetricProperty"].is_string() ||
            !metricValue.contains("MetricValue"))
        {
            continue;
        }

        std::string fromUrl =
            metricValue["MetricProperty"].get_ref<const std::string&>();
        if (size_t hashPos = fromUrl.find('#'); hashPos != std::string::npos)
        {
            fromUrl = fromUrl.substr(0, hashPos);
        }

        auto itSensor = sensors.find(fromUrl);
        if (itSensor != sensors.end())
        {
            // Already created: the report carries only a bare value.
            double val = parseMetricValue(metricValue["MetricValue"]);
            ctx.spawn(itSensor->second->updateValue(val));
            continue;
        }

        // Not yet created. Find the configured mapper for this URL (cold path,
        // happens at most once per sensor) and read it in full to establish its
        // unit and range.
        auto it =
            std::ranges::find_if(config.mappers, [&](const auto& candidate) {
                return candidate.fromUrl == fromUrl;
            });
        if (it == config.mappers.end())
        {
            continue;
        }
        const SensorMapper* mapper = &*it;

        // Reports drive this sensor from now on, so stop polling it
        // individually even if this first read fails.
        reportCovered.insert(mapper->fromUrl);

        // One-shot read to bootstrap the sensor; reports drive it afterwards,
        // so don't cache the connection.
        auto maybeSensor = co_await fetchSensor(ctx, mapper->fromUrl,
                                                /*cacheConnection=*/false);
        if (maybeSensor.has_value())
        {
            updateSensor(ctx, *mapper, maybeSensor.value());
        }
    }
    co_return;
}

void SensorHandler::updateSensor(sdbusplus::async::context& ctx,
                                 const SensorMapper& mapper,
                                 redfish_binding::Sensor::Sensor& parsed)
{
    auto it = sensors.find(mapper.fromUrl);
    if (it != sensors.end())
    {
        // Already created; push the new reading value.
        double val = parsed.getReading().hasValue()
                         ? parsed.getReading().value()
                         : std::numeric_limits<double>::quiet_NaN();
        ctx.spawn(it->second->updateValue(val));
        return;
    }

    // mapper.toNamespace is validated against the supported namespaces when the
    // config is parsed, so it is passed through verbatim.
    auto sensor =
        Sensor::create(ctx, mapper.toNamespace, mapper.toId,
                       config.associationPath, parsed, mapper.thresholds);
    if (sensor)
    {
        sensors.emplace(mapper.fromUrl, std::move(sensor));
    }
}

} // namespace redfish_client::core
