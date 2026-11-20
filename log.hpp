#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <cstdio>
#include <exception>

// 级别判断在<<表达式之前
#define MYLOG_DETAIL_LOG(level) \
    if (!::Logger::get_instance().enabled(level)) {} \
    else ::LogStream(level, __FILE__, __LINE__, __func__)

#define LOG_DBG()  MYLOG_DETAIL_LOG(::Log_Debug)
#define LOG_INFO() MYLOG_DETAIL_LOG(::Log_Info)
#define LOG_WARN() MYLOG_DETAIL_LOG(::Log_Warning)
#define LOG_ERR()  MYLOG_DETAIL_LOG(::Log_Error)

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const std::string& msg) = 0;
    virtual void flush() {}
};

class ConsoleSink : public Sink {
public:
    void write(const std::string& msg) override {
        std::cout << msg;
        if (!std::cout) {
            throw std::ios_base::failure("could not write to standard output");
        }
    }

    void flush() override {
        std::cout.flush();
        if (!std::cout) {
            throw std::ios_base::failure("could not flush standard output");
        }
    }
};

class FileSink : public Sink {
public:
    explicit FileSink(const std::string& file_name)
        : _file_name(file_name), _ofs(_file_name, std::ios::app) {
        if (!_ofs) {
            throw std::ios_base::failure("could not open log file: " + _file_name);
        }
    }

    void write(const std::string& msg) override {
        _ofs << msg;
        if (!_ofs) {
            throw std::ios_base::failure("could not write log file: " + _file_name);
        }
    }

    void flush() override {
        _ofs.flush();
        if (!_ofs) {
            throw std::ios_base::failure("could not flush log file: " + _file_name);
        }
    }

private:
    std::string _file_name;
    std::ofstream _ofs;
};

enum Log_Level
{
    Log_Debug = 0,
    Log_Info = 1,
    Log_Warning = 2,
    Log_Error = 3,
};

class Logger {
private:
    std::atomic<Log_Level> _log_min_level{Log_Info};

    std::mutex _mutex;

    std::vector<std::unique_ptr<Sink>> _sinks;

private:
    Logger() {
        _sinks.emplace_back(std::make_unique<ConsoleSink>());
    }

public:
    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }

    void set_level(Log_Level level) {
        std::lock_guard<std::mutex> lock(_mutex);

        _log_min_level.store(level, std::memory_order_relaxed);
    }

    bool enabled(Log_Level level) const noexcept {
        return level >= _log_min_level.load(std::memory_order_relaxed);
    }

    void add_file_sink(const std::string& filename) {
        std::lock_guard<std::mutex> lock(_mutex);

        _sinks.emplace_back(std::make_unique<FileSink>(filename));
    }

    void write(Log_Level level, const char* file, int line, const char* func, const std::string& msg) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!enabled(level)) return;

        const std::string str = format_msg(level, file, line, func, msg);
        for_each_sink([&str](Sink& sink) { sink.write(str); });
    }

    void flush() {
        std::lock_guard<std::mutex> lock(_mutex);
        for_each_sink([](Sink& sink) { sink.flush(); });
    }

private:
    // 对每个已注册的输出端执行同一个操作，并确保某个输出端失败后，后面的输出端仍有机会执行.
    // 会在所有输出端尝试过后，输出第一个异常。
    template<typename Operation>
    void for_each_sink(Operation operation) {
        std::exception_ptr first_error;
        for (auto& sink : _sinks) {
            try {
                operation(*sink);
            } catch (...) {
                if (!first_error) {
                    first_error = std::current_exception();
                }
            }
        }
        if (first_error) {
            std::rethrow_exception(first_error);
        }
    }

    std::string format_msg(Log_Level level, const char* file, int line, const char* func, const std::string& msg) {
        std::ostringstream oss;

        oss << "[" << now_time() << "]"
            << "[" << level_to_string(level) << "]"
            << "[" << file << ":" << line << "]"
            << "[" << func << "] "
            << msg;

        // 按需补换行，防止出现多一个或者没有换行的情况
        if (msg.empty() || msg.back() != '\n') {
            oss.put('\n');
        }

        return oss.str();
    }

    std::string now_time() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::string level_to_string(Log_Level level) {
        switch(level) {
        case Log_Debug: return "Debug";
        case Log_Info: return "Info";
        case Log_Warning: return "Warning";
        case Log_Error: return "Error";
        }
        return "";
    }
};

class LogStream {
public:
    LogStream(Log_Level level, const char* file, int line, const char* func)
        : _level(level), _file(file), _line(line), _func(func) {}

    ~LogStream() noexcept {
        try {
            Logger::get_instance().write(_level, _file, _line, _func, _buffer.str());
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Error: %s\n", error.what());
        } catch (...) {
            std::fputs("Error: log write failed with an unknown exception\n", stderr);
        }
    }

    template<typename T>
    LogStream& operator<<(T&& msg) {
        _buffer << msg;
        return *this;
    }

    // 重载operator<<，接受操作输出流函数，如:std::endl, std::flush
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        manip(_buffer);
        return *this;
    }

private:
    std::ostringstream _buffer;
    Log_Level _level;
    const char* _file;
    int _line;
    const char* _func;
};
