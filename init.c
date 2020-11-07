#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include<string.h>
#include<stdlib.h>

typedef struct login{
	char username[100];
	char password[100];
	char type;
    	long long int accno;
	char active;
}admin;

typedef struct seq{
    long long int count;
}acc_num;

int main(){
    // setup admin
    int fd = open("login.dat", O_CREAT | O_WRONLY,0766);
    admin a = {"meghna","1234",'A','1','y'};
    write(fd,&a,sizeof(a));
    close(fd);

    // set initial account num
    fd = open("acc_num", O_CREAT | O_WRONLY,0766);
    
    acc_num num;
    
    long long int x = 100000;
    num.count = x;
    lseek(fd,0,SEEK_SET);
    write(fd,&num,sizeof(num));
    
    close(fd);
    return 0;  
}
