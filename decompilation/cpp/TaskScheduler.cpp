#include "MappedTaskScheduler.h"
#include <cstring>
#include <bit>

namespace trilogy::original {
void TaskScheduler::reset() noexcept {
    head=0; cursor=0; freeHead=0;
    for (std::uint32_t i=0;i<256;++i) {
        auto address=poolBase+i*32u;
        node(address).next=freeHead;
        freeHead=address;
    }
}

void TaskScheduler::insert(TaskAddress address) noexcept {
    auto& task=node(address);
    task.next=0; task.previous=0;
    if (!head) {head=address; return;}
    if (task.priority<node(head).priority) {
        task.next=head; node(head).previous=address; head=address; return;
    }
    auto previous=head;
    while (node(previous).next) {
        auto next=node(previous).next;
        if (task.priority<node(next).priority) {
            node(previous).next=address;
            node(next).previous=address;
            task.next=next;
            task.previous=previous;
            return;
        }
        previous=next;
    }
    node(previous).next=address;
    task.previous=previous;
}

TaskAddress TaskScheduler::create(std::int32_t priority,std::uint32_t callback) noexcept {
    auto task=freeHead;
    if (task) {
        freeHead=node(task).next;
        node(task).priority=priority;
        node(task).callbackAddress=callback;
        insert(task);
    }
    return task;
}

void TaskScheduler::remove(TaskAddress address) noexcept {
    auto& task=node(address);
    auto next=task.next, previous=task.previous;
    cursor=next;
    if (next) node(next).previous=previous;
    if (previous) node(previous).next=next;
    else head=next;
    task.next=0; task.previous=0;
    task.next=freeHead;
    freeHead=address;
}

void TaskScheduler::advance(TaskAddress task) noexcept {cursor=node(task).next;}

void TaskScheduler::dispatch(const Invoke& invoke) {
    auto task=head;
    while (task) {invoke(*this,task); task=cursor;}
}

void TaskScheduler::dispatchCompensated(std::uint32_t timing,bool compensation,const Invoke& invoke) {
    // Original INC wraps at 32 bits, SAR then uses only the low byte.
    auto iterations=static_cast<std::uint8_t>((timing+1u)>>1);
    if (!iterations || !compensation) iterations=1;
    do {
        remainingTicks=iterations;
        dispatch(invoke);
        iterations=static_cast<std::uint8_t>(remainingTicks-1u);
    } while (iterations);
    remainingTicks=1;
}
PlayerControlService withMappedTaskScheduler(const PlayerControlService& external) {
    return [external](PlayerControlMemory& m,unsigned routine,unsigned a,unsigned b,unsigned c) {
        if(routine!=0x12258&&routine!=0x122c0&&routine!=0x12364&&routine!=0x12394&&routine!=0x13f34)
            return external(m,routine,a,b,c);
        TaskScheduler scheduler(m.d(0x22ceb4));
        std::memcpy(scheduler.nodes.data(),m.bytes.data()+scheduler.poolBase,sizeof(scheduler.nodes));
        scheduler.head=m.d(0x22ceb8);scheduler.freeHead=m.d(0x22cebc);scheduler.cursor=m.d(0x22cec0);
        unsigned result=0;
        switch(routine){
            case 0x12258:scheduler.reset();break;
            case 0x122c0:scheduler.insert(a);break;
            case 0x12364:result=scheduler.create(std::bit_cast<std::int32_t>(a),b);break;
            case 0x12394:scheduler.remove(a);break;
            case 0x13f34:scheduler.advance(a);break;
        }
        std::memcpy(m.bytes.data()+scheduler.poolBase,scheduler.nodes.data(),sizeof(scheduler.nodes));
        m.d(0x22ceb8,scheduler.head);m.d(0x22cebc,scheduler.freeHead);m.d(0x22cec0,scheduler.cursor);
        return PlayerControlResult{result,0};
    };
}
}
