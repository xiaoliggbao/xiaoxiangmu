#pragma once
#include<pthread.h>
#include<string>
#include<map>
using namespace std;
struct User{
    int id=0;
    string password_hash;
};
class UserStore{
public:
    UserStore();
    ~UserStore();
    UserStore(const UserStore&)=delete;
    int registerUser(const string& username, const string& password,int& out_id);
    int login(const string& username, const string& password,int& out_id);
private:
    pthread_mutex_t mutex;
    //这个<>括号里的string是key的类型，User是value的类型
    //map 是红黑树，不是哈希表；它按 key 自动排序
    map<string,User> users;//键值对
    int next_id=1001;
};