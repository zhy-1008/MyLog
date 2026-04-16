#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <iomanip>

#define LOG_DBG()  LogStream(Log_Debug, __FILE__, __LINE__, __func__)
#define LOG_INFO()  LogStream(Log_Info, __FILE__, __LINE__, __func__)
#define LOG_WARN() LogStream(Log_Warning, __FILE__, __LINE__, __func__)
#define LOG_ERR()  LogStream(Log_Error, __FILE__, __LINE__, __func__)

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const std::string& msg) = 0;
};

class ConsoleSink : public Sink {
public:
    void write(const std::string& msg) {
        std::cout << msg;
    }
};

class FileSink : public Sink {
public:
    // explicit ???   std::ios::app ???
    explicit FileSink(const std::string& file_name) {
        _ofs.open(file_name, std::ios::app);
    }

    void write(const std::string& msg) {
        _ofs << msg;
    }

private:
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
    Log_Level _log_min_level = Log_Info;

    std::mutex _mutex;

    std::vector<std::unique_ptr<Sink>> _sinks;

private:
    Logger() {
        _sinks.emplace_back(std::make_unique<ConsoleSink>());
    }

public:
    static Logger& get_instace() {
        static Logger instance;
        return instance;
    }

    void set_level(Log_Level level) {
        std::lock_guard<std::mutex> lock(_mutex);

        _log_min_level = level;
    }

    void add_file_sink(const std::string& filename) {
        std::lock_guard<std::mutex> lock(_mutex);

        _sinks.emplace_back(std::make_unique<FileSink>(filename));
    }

    void write(Log_Level level, const char* file, int line, const char* func, const std::string& msg) {
        std::lock_guard<std::mutex> lock(_mutex);

        if(level < _log_min_level) return;

        std::string str = format_msg(level, file, line, func, msg);

        for(auto& sink : _sinks) {
            sink->write(str);
        }
    }

private:
    std::string format_msg(Log_Level level, const char* file, int line, const char* func, const std::string& msg) {
        std::ostringstream oss;

        oss << "[" << now_time() << "]"
            << "[" << level_to_string(level) << "]"
            << "[" << file << ":" << line << "]"
            << "[" << func << "] "
            << msg
            << "\n";

        return oss.str();
    }

    std::string now_time() {
    // std::chrono::system_clock::now std::chrono::system_clock::to_time_t ???
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);

    // std::tm localtime_s localtime_r ???
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    // #include <iomanip> std::put_time ???
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

    ~LogStream() {
        Logger::get_instace().write(_level, _file, _line, _func, _buffer.str());
    }

    template<typename T>
    LogStream& operator<<(T&& msg) {
        _buffer << msg;
        return *this;
    }

private:
    std::ostringstream _buffer;
    Log_Level _level;
    const char* _file;
    int _line;
    const char* _func;
};