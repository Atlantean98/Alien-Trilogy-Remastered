#pragma once
#include "ExplorerAssets.h"
#include <functional>
#include <string>
#include <vector>
class ExplorerPickups {
public:
    struct QueuedMessage {std::wstring text;unsigned length{},age{};};
    const std::vector<QueuedMessage>& networkMessages()const{return messages;}
    void networkMessages(const std::vector<QueuedMessage>&);
    void load(trilogy::original::PlayerControlMemory);
    void clear();
    std::wstring tick(int x,int y,int z);
    void trigger(unsigned delta,unsigned index);
    void crateDrop(unsigned object,unsigned index);
    void enemyDrop(unsigned entity);
 void dropSurvivalPickup(unsigned kind,unsigned amount,unsigned reserve,unsigned x,unsigned z);
    std::vector<unsigned> active()const;
    unsigned messageAge()const{return messages.empty()?0u:messages.front().age;}
    const std::wstring& messageText()const{static const std::wstring empty;return messages.empty()?empty:messages.front().text;}
    unsigned queuedCount()const{return unsigned(messages.size());}
    unsigned remainingTicks()const{return messages.empty()?0u:(messages.front().length+40u-messages.front().age);}
    std::function<void(unsigned)> sound;
    unsigned collected{};unsigned lastKind=255;int lastX=0,lastZ=0;   // the last pickup collected: kind, where
    static const wchar_t* name(unsigned kind);
private:
    void syncMessageTable();
    std::wstring advanceMessages();
    trilogy::original::PlayerControlMemory m{};
    std::vector<QueuedMessage> messages;
};
