
for (int i=0; i<5; i++){
    if (!fork()){
        sleep (i+1);
    }
    wait (0);
}

int fd1 = open("file.txt", O_RDWR|O_CREAT, 0700); // open in read+write mode
int fd2 = open("file.txt", O_RDWR|O_CREAT, 0700);
char* str1 = "Gig'em Aggies, ";
char str2 = "BTHO 'Bama ";
char* str3 = "We did it ";
write (fd1, str1, strlen(str1));
int fd3 = dup(fd2);
dup2 (fd1, fd2);
if (!fork()){
    dup2 (fd1, 1);
    cout << str2;
    write (fd2, str2, strlen(str2));
}else{
    wait(0);
    write (fd3, str3, strlen(str3));
}

// these are globals accessible to both threads
bool done = false;
queue<int> list;
Semaphore s(0);
mutex m;
void Producer/Push (){
  int data = getdatafrmsrc();
  m.lock();
  list.push_back (data);
  m.unlock();
  s.V();
}
void Consumer/Pop (){
  s.P();
  m.lock();
  int data = list.front();
  list.pop();
  m.unlock();
}
