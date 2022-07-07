#include "twTimer.h"
#include "../server/utils.h"

/* 初始化 */
SortTimerWheel::SortTimerWheel(): _curSlot(0){
    for(int i=0; i<N; i++){
        _slots[i] = NULL;
    }
}

/* 析构， 删除每一个定时器 */
SortTimerWheel::~SortTimerWheel(){
    for(int i=0; i<N; i++){
        UtilTimer* tmp = _slots[i];
        while(tmp){
            _slots[i] = tmp->_next;
            delete tmp;
            tmp = _slots[i];
        }
    }
}

/* 创建一个新的定时器，并将其加入适当的槽中 */
UtilTimer* SortTimerWheel::addTimer(int time){
    if(time < 0){
        return NULL;
    }
    int ticks = 0;
    /* 下面根据定时器时间值time来设定该定时器的时长是多少个时间槽间隔 */
    if(time < SI){
        ticks = 1;
    }
    else{
        ticks = time / SI;
    }

    /* 计算定时器时长对应的圈数以及应该被插在哪个时间槽中 */
    int rotation = ticks / N;
    int ts = (_curSlot + (ticks%N)) % N;
    /* 创建新的定时器 */
    UtilTimer* timer = new UtilTimer(rotation, ts);

    /* 将定时器插入对应的槽中 */
    /* 该槽没有定时器 */
    if( !_slots[ts] ){
        _slots[ts] = timer;
    }
    /* 该槽已有定时器 */
    else{
       timer->_next = _slots[ts];
       _slots[ts]->_prev = timer;
       _slots[ts] = timer;
    }
    return timer;
}

/* 延长定时器时间，调整定时器的位置 */
void SortTimerWheel::adjustTimer(UtilTimer* timer, int time){
    if(time < 0){
        return;
    }
    int ts = timer->_slot;
    /* 移除该定时器 */
    /* 如果该定时器是时间槽的头结点 */
    if(timer == _slots[ts]){
        _slots[ts] = _slots[ts]->_next;
        if(_slots[ts]){
            _slots[ts]->_prev = NULL;
        }
    }
    /* 不是头结点 */
    else{
        timer->_prev->_next = timer->_next;
        if(timer->_next){
            timer->_next->_prev = timer->_prev;
        }
    }

    /* 下面根据定时器时间值time来设定该定时器的时长是多少个时间槽间隔 */
    int ticks = 0;
    if(time < SI){
        ticks = 1;
    }
    else{
        ticks = time / SI;
    }
    /* 计算定时器时长对应的圈数以及应该被插在哪个时间槽中 */
    int rotation = timer->_rotation +  (ticks + timer->_slot) / N;
    ts = (timer->_slot + ticks) % N;
    timer->_rotation = rotation;
    timer->_slot = ts;
    /* 将定时器插入对应的槽中 */
    /* 该槽没有定时器 */
    if( !_slots[ts] ){
        _slots[ts] = timer;
    }
    /* 该槽已有定时器 */
    else{
       timer->_next = _slots[ts];
       _slots[ts]->_prev = timer;
       _slots[ts] = timer;
    }

}

/* 删除定时器 */
void SortTimerWheel::deleteTimer(UtilTimer* timer){
    if( !timer ){
        return;
    }
    int ts = timer->_slot;
    /* 如果该定时器是时间槽的头结点 */
    if(timer == _slots[ts]){
        _slots[ts] = _slots[ts]->_next;
        if(_slots[ts]){
            _slots[ts]->_prev = NULL;
        }
        delete timer;
    }
    /* 不是头结点 */
    else{
        timer->_prev->_next = timer->_next;
        if(timer->_next){
            timer->_next->_prev = timer->_prev;
        }
        delete timer;
    }
}

/* 时间到，删除超时的定时器，时针向前滚动一个时间槽 */
void SortTimerWheel::tick(){
    /* 取得时间轮当前槽的头结点, 依次检查转数是否为0 */
    UtilTimer* tmp = _slots[_curSlot];
    while(tmp){
        /* 该定时器转数大于0，说明还剩余时间 */
        if(tmp->_rotation > 0){
            tmp->_rotation--;
            tmp = tmp->_next;
        }
        /* 否则说明定时器到期，执行定时任务，删除定时器 */
        else{
            tmp->_cbFunc(tmp->_userData);
            if(tmp == _slots[_curSlot]){
                _slots[_curSlot] = tmp->_next;
                delete tmp;
                if(_slots[_curSlot]){
                    _slots[_curSlot]->_prev = NULL;
                }
                tmp = _slots[_curSlot];
            }
            else{
                tmp->_prev->_next = tmp->_next;
                if(tmp->_next){
                    tmp->_next->_prev = tmp->_prev;
                }
                UtilTimer* tmp2 = tmp->_next;
                delete tmp;
                tmp = tmp2;
            }
        }
    }
    /* 时间流逝，时针向前转动一槽 */
    _curSlot = ++_curSlot % N;
}




/*
*功能：超时信号的回调函数，将用户从epoll监听数据中删除，并关闭
*参数：  --uesrData：超时的连接
*/
void cb_func(ClientData* userData){
    epoll_ctl(Utils::_epollFd, EPOLL_CTL_DEL, userData->_sockFd, NULL);
    assert(userData);
    close(userData->_sockFd);
    HttpConn::_userCount--;
}