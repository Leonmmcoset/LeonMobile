#pragma once
#include <atomic>
#include <future>
#include <leon/root.hpp>
#include <lmui/lmui.hpp>
class RootDemo {
  public:
    RootDemo();
    ~RootDemo();
    int run();

  private:
    lmui::Application app{"org.example.rootdemo", "Root 权限示例"};
    lmui::Widget actions;
    lmui::TouchInput touch;
    std::future<leon::RootResult> job;
    std::stop_source stop;
    std::atomic<leon::RootPhase> phase{leon::RootPhase::pending};
    leon::RootResult result;
    bool attempted{};
    void request();
    void paint(lmui::Canvas &);
};
