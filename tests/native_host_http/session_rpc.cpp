#include "backend/sessionapirpc.h"
#include <cassert>
#include <future>
#include <iostream>
#include <vector>
using namespace std::chrono_literals;
int main() {
    SessionApiRpc::Broker broker;
    std::mutex mutex; std::condition_variable cv;
    std::vector<uint64_t> ids;
    broker.install([&](uint64_t id,const std::string&) {
        std::lock_guard<std::mutex> lock(mutex); ids.push_back(id);cv.notify_all();return true;
    });
    auto first=std::async(std::launch::async,[&]{return broker.call("first",2000);});
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,1s,[&]{return ids.size()==1;}));}
    auto second=std::async(std::launch::async,[&]{return broker.call("second",2000);});
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,1s,[&]{return ids.size()==2;}));}
    assert(broker.complete(ids[1],"second-result"));
    assert(!broker.complete(ids[1],"duplicate"));
    assert(broker.complete(ids[0],"first-result"));
    assert(first.get()=="first-result" && second.get()=="second-result");
    assert(!broker.complete(ids[0],"late"));
    try {broker.call("timeout",20);assert(false);} catch(const std::runtime_error&) {}
    assert(!broker.complete(ids.back(),"expired"));
    auto waiting=std::async(std::launch::async,[&]{try {broker.call("pending",2000);return false;} catch(const std::runtime_error&) {return true;}});
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,1s,[&]{return ids.size()==4;}));}
    broker.close(); assert(waiting.get()); assert(broker.enabled());
    try {broker.call("after-close",20);assert(false);} catch(const std::runtime_error&) {}
    SessionApiRpc::Broker failed;
    failed.install([](uint64_t,const std::string&){return false;});
    try {failed.call("pipe-failure",20);assert(false);} catch(const std::runtime_error&) {}
    std::cout<<"Concurrent/out-of-order, duplicate/late, timeout, shutdown and pipe failure passed\n";
}
