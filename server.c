#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define USER 0
#define ADMIN 2
#define UNAUTH_USER -1
#define RESPONSE_BYTES 512
#define REQUEST_BYTES 512
#define linesInMS 5
#define EXIT -1

typedef struct login{
	char username[100];
	char password[100];
	char type;
	long long int accno;
	char active;
}user;

typedef struct seq{
    long long int count;
}acc_num;

void talkToClient(int client_fd);
void closeclient(int client_fd,char *str);

//-----------------------------------------------------------------------------
//Communication with client

void sendMsgtoClient(int clientFD, char *str) {
    int numPacketsToSend = (strlen(str)-1)/RESPONSE_BYTES + 1;
    int n = write(clientFD, &numPacketsToSend, sizeof(int));
    char *msgToSend = (char*)malloc(numPacketsToSend*RESPONSE_BYTES);
    strcpy(msgToSend, str);
    int i;
    for(i = 0; i < numPacketsToSend; ++i) {
        int n = write(clientFD, msgToSend, RESPONSE_BYTES);
        msgToSend += RESPONSE_BYTES;
    }
}
char* recieveMsgFromClient(int clientFD) {
    int numPacketsToReceive = 0;
    int n = read(clientFD, &numPacketsToReceive, sizeof(int));
    if(n <= 0) {
        shutdown(clientFD, SHUT_WR);
        return NULL;
    }
    char *str = (char*)malloc(numPacketsToReceive*REQUEST_BYTES);
    memset(str, 0, numPacketsToReceive*REQUEST_BYTES);
    char *str_p = str;
    int i;
    for(i = 0; i < numPacketsToReceive; ++i) {
        int n = read(clientFD, str, REQUEST_BYTES);
        str = str+REQUEST_BYTES;
    }
    return str_p;
}

//-----------------------------------------------------------------------------
// convert long long int to str

char* strfromlonglong(long long int value){
  char buf[32], *p;
    long long int v;

  v = (value < 0) ? -value: value;
  p = buf + 31;
  do{
    *p -- = '0' + (v%10);
    v /= 10;
  } while(v);
  if(value < 0) *p -- = '-';
  p ++;

  int len = 32 - (p - buf);
  char *s = (char*)malloc(sizeof(char) * (len + 1));
  memcpy(s, p, len);
  s[len] = '\0';
  return s;
}

//-----------------------------------------------------------------------------
//Get account number

char* accountFromName(char *username){
	user usr;
	int fd = open("login.dat",O_RDONLY);
	while((read(fd,&usr,sizeof(usr)))>0){
		if(strcmp(usr.username,username)==0){
			char *buff = NULL;
			buff = strfromlonglong(usr.accno);
			return buff;
		}
	}
}

//-----------------------------------------------------------------------------
//Get mini statement

char *printMiniStatement(char *db,int client_fd)
{
	int fp = open(db, O_RDONLY);
	char *current_balance = (char *)malloc(20*sizeof(char));
	lseek(fp, 20, SEEK_CUR);
	char *miniStatement = (char *)malloc(1000*sizeof(char));
	read(fp, miniStatement, 1000);
	return miniStatement;
}

//-----------------------------------------------------------------------------
//get current balance from DB

char *printBalance(char *db)
{
	int fd = open(db, O_RDONLY);
	char *current_balance = (char *)malloc(20*sizeof(char));
	read(fd, current_balance, 20);
	return current_balance;
}

//-----------------------------------------------------------------------------
//Update the database

void updateTrans(char *db,char c,double balance,double amount)
{
	
	int fp = open(db, O_WRONLY | O_APPEND);
	int fpb = open(db, O_WRONLY);
	char *buff = (char *)malloc(20*sizeof(char));
	write(fpb, buff, 20);
	
	int length = sprintf(buff, "%20f",balance);
	lseek(fpb, 0, 0);
	write(fpb, buff, length);
	close(fpb);	
	
	char buffer[100];
	time_t ltime; /* calendar time */
	ltime=time(NULL); /* get current cal time */

	length = sprintf(buffer,"%.*s %f %c %f\n",(int)strlen(asctime(localtime(&ltime)))-1 , asctime(localtime(&ltime)), balance, c,amount);
	
	write(fp, buffer, length);
	close(fp);
	free(buff);	
}

//-----------------------------------------------------------------------------
//DEbit

int Debit(char *username, int client_fd){

char usrupdate[] = "Amount debited successfully!\n-----------------------------------\n\n\nDo you wish to continue?\n";
char usrupdateu[] = "Your balance is low\n-----------------------------------\n\n\nWrite exit to quit\n";

	char *db = accountFromName(username);
	int fd = open(db, O_RDWR);

	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 20;
	// blocking mode F_SETLK
	if(fcntl(fd, F_SETLK, &fl) == -1){
		sendMsgtoClient(client_fd, "\n\nAnother Transaction is in progress. Write exit to quit\n");
		close(fd);
		return 0;
		printf("cannot write lock\n");
		exit(1);
	}

	double balance=strtod(printBalance(db),NULL);
	double amount=0.0;
	sendMsgtoClient(client_fd, "Enter Amount to withdraw: ");
	while(1){
		char *buff=recieveMsgFromClient(client_fd);
		amount=strtod(buff,NULL);
		if(amount<=0)
			sendMsgtoClient(client_fd,"Enter valid amount: ");
		else
		   break;
		}
		if(balance >= amount){
			balance -=amount;
			updateTrans(db,'D',balance,amount);
			sendMsgtoClient(client_fd,usrupdate);
		}
			
		else{
			sendMsgtoClient(client_fd,usrupdateu);					
		}
	
	fl.l_type = F_UNLCK;
	if(fcntl(fd, F_SETLK, &fl) == -1){
		printf("unlocked fail\n");
		exit(1);
	}
	
	close(fd);
	//printf("User DB updated\n");
}

//-----------------------------------------------------------------------------
//Credit

int Credit(char *username, char *password, int client_fd){

    char usrupdate[] = "Amount credited successfully!\n-----------------------------------\nDo you want to continue?";

	// get account name from username
	char *db = accountFromName(username);
	int fd = open(db, O_RDWR);

	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 20;
	// blocking mode F_SETLK
	if(fcntl(fd, F_SETLK, &fl) == -1){
		printf("trying to open %s",db);
		sendMsgtoClient(client_fd, "\n\nAnother Transaction is Being Processed.\n-----------------------------------\nDo you want to continue?");
		
		close(fd);
		return 0;
		printf("cannot write lock\n");
		exit(1);
	}

	double balance=strtod(printBalance(db),NULL);
	double amount=0.0;
	sendMsgtoClient(client_fd, "Enter Amount: ");
	while(1){
		char *buff=recieveMsgFromClient(client_fd);
		amount=strtod(buff,NULL);
		if(amount<=0)
		sendMsgtoClient(client_fd,"Enter valid amount: \n");
		else
		   break;
		}
	balance +=amount;
	updateTrans(db,'C',balance,amount);
	sendMsgtoClient(client_fd,usrupdate);
	
	fl.l_type = F_UNLCK;
	if(fcntl(fd, F_SETLK, &fl) == -1){
		printf("unlocked fail\n");
		exit(1);
	}
	close(fd);
}

//----------------------------------------------------------------
//get new account number

long long int getAccNo(){
	int fd = open("acc_num",O_RDWR);
	acc_num num;
    read(fd,&num,sizeof(num));
    long long int x = num.count+1;
    num.count = x;
    lseek(fd,0,SEEK_SET);
    write(fd,&num,sizeof(num));
	close(fd);
    return x;  
}

//--------------------------------------------------------------------
// check if username already exists

int checkUser(char *username)
{
	user usr;
	int fd = open("login.dat", O_RDONLY);
	while((read(fd, &usr, sizeof(usr))) > 0) {
		if(strcmp(usr.username,username)==0){	
				close(fd);
				return 1;	
        }
    }
    close(fd);
    return 0;
}

//-------------------------------------------------------------------------
// Create a new account

void AddUser(int client_fd){

	user usr;
	sendMsgtoClient(client_fd,"Enter username: ");
	char *name = NULL;
	NAME:
	name = NULL;
	name = recieveMsgFromClient(client_fd);
	if(checkUser(name)){
		sendMsgtoClient(client_fd, "UserName already exists\nEnter username: ");	
		goto NAME;		
	}
	
	sendMsgtoClient(client_fd, "Enter Password: ");
	char *pass = NULL, *cpass = NULL;
	PASS:
	pass = recieveMsgFromClient(client_fd);
	sendMsgtoClient(client_fd, "Re-enter New Password: ");
	cpass = recieveMsgFromClient(client_fd);
	if(strcmp(pass,cpass) == 0){ 
		// passwords match, proceed with creating account
		strcpy(usr.username,name);
		strcpy(usr.password,pass);
		usr.accno = getAccNo();
		usr.type = 'C';
		usr.active = 'y';

		// create entry in login.dat
		int fd = open("login.dat", O_WRONLY | O_APPEND);
		write(fd, &usr, sizeof(usr));
		close(fd);

		//create account file
		char *filename = NULL;
		filename = strfromlonglong(usr.accno);
		creat(filename, 0766);
		fd = open(filename, O_WRONLY | O_APPEND, 0766);
		double balance = 1000.000000000000000;
		char buff[20] = {0};
		sprintf(buff, "%f", balance);
		write(fd, buff, sizeof(buff));
		close(fd);
	}
	else{
		sendMsgtoClient(client_fd, "Passwords do not match\nEnter New Password: ");
		goto PASS;			
	}

}

//-------------------------------------------------------------------
// Create a joint account

void jointAcc(int client_fd){

	char *name1, *pass1, *name2, *pass2, *cpass;
	sendMsgtoClient(client_fd,"Enter 1st username: ");
	name1 = NULL;
	NAME:				
	name1 = recieveMsgFromClient(client_fd);
	if(checkUser(name1)){
		sendMsgtoClient(client_fd, "UserName already exists\nEnter username: ");	
		goto NAME;		
	}
	sendMsgtoClient(client_fd, "Enter Password: ");
	PASS:
	pass1 = recieveMsgFromClient(client_fd);
	sendMsgtoClient(client_fd, "ReEnter New Password: ");
	cpass = recieveMsgFromClient(client_fd);

	// 1st user creds verified
	if(strcmp(pass1,cpass) == 0){
		name2 = NULL;
		cpass = NULL;
		sendMsgtoClient(client_fd,"Enter 2nd username: ");
		NAME2:				
		name2 = recieveMsgFromClient(client_fd);
		if(checkUser(name2)){
			sendMsgtoClient(client_fd, "UserName already exists\nEnter username: ");	
			goto NAME2;		
		}
		sendMsgtoClient(client_fd, "Enter Password: ");
		PASS2:
		pass2 = recieveMsgFromClient(client_fd);
		sendMsgtoClient(client_fd, "ReEnter New Password: ");
		cpass = recieveMsgFromClient(client_fd);

		// user 2 password verified	
		if(strcmp(pass2,cpass)==0){
			// added both the users in login.dat	
			user usr1, usr2;
			long long int no = getAccNo();

			strcpy(usr1.username, name1);
			strcpy(usr1.password, pass1);
			usr1.type = 'J';
			usr1.active = 'y';
			usr1.accno = no;

			strcpy(usr2.username, name2);
			strcpy(usr2.password, pass2);
			usr2.type = 'J';
			usr2.active = 'y';
			usr2.accno = no;

			int fd = open("login.dat", O_WRONLY | O_APPEND);
			write(fd, &usr1,  sizeof(usr1));
			write(fd, &usr2,  sizeof(usr2));
			close(fd);

			char *filename = NULL;
			filename = strfromlonglong(no);
			creat(filename, 0766);
			fd = open(filename, O_WRONLY | O_APPEND, 0766);
			double balance = 1000.000000000000000;
			char buff[20] = {0};
			sprintf(buff, "%f", balance);
			write(fd, buff, sizeof(buff));
			close(fd);
			}
			// 2nd user's password not verified
			else{
				sendMsgtoClient(client_fd, "Passwords do not match\nEnter New Password: ");
				goto PASS2;			
			}
		}

		// 1st user not verified
		else{
			sendMsgtoClient(client_fd, "Passwords do not match\nEnter New Password: ");
			goto PASS;			
		}
}
//----------------------------------------------------------
// change password

int Passwordchng(char *username, int client_fd){
char usrupdate[] = "Password updated successfully.\n-----------------------------------\n\nDo you want to continue?";

	int fd = open("login.dat", O_RDWR);
	user usr;
	char *pass = NULL;
	char *cpass = NULL;
	while(read(fd, &usr, sizeof(usr)) > 0){
		if(strcmp(usr.username, username) == 0){
			sendMsgtoClient(client_fd, "Enter New Password: ");
			ReEntry:
			pass = recieveMsgFromClient(client_fd);
			sendMsgtoClient(client_fd, "ReEnter New Password: ");
			cpass = recieveMsgFromClient(client_fd);
			if(strcmp(pass,cpass) == 0){
				lseek(fd, -1*sizeof(user), SEEK_CUR);
				strcpy(usr.password, pass);
				write(fd, &usr, sizeof(usr));
				sendMsgtoClient(client_fd, usrupdate);
				return 1;		
			}
			else{
				sendMsgtoClient(client_fd, "Passwords do not match\nEnter New Password: ");
				goto ReEntry;			
			}
		}
	
	}
	return 0;
}

//--------------------------------------------------------------
//Delete user

void deleteUser(int client_fd,char* name){
	int fd = open("login.dat",O_RDWR);
	char *buff= NULL;
	user usr;
	if(!checkUser(name))
	{
		sendMsgtoClient(client_fd, "Entered user not found.\n-----------------------------------\n\n");
	}
	else{
	
		while(read(fd,&usr,sizeof(usr))>0)
		{
			if(strcmp(name,usr.username) == 0)
			{
				sendMsgtoClient(client_fd, "Are you sure you want to delete the user? Type yes/no");
				buff = recieveMsgFromClient(client_fd);
				if(strcmp(buff,"yes")==0)
				{
					usr.active = 'n';
					lseek(fd, -1*sizeof(usr), SEEK_CUR);
					write(fd, &usr, sizeof(usr));
					sendMsgtoClient(client_fd, "User account deleted successfully!\n-----------------------------------\n\n");
					break;
				}
			}
		}
	}
	close(fd);
}

//--------------------------------------------------------------
//Get user details

void userDetails(int client_fd, char *u){
	
	if(checkUser(u)){
		int fd = open("login.dat",O_RDONLY);
		user usr;
		while((read(fd,&usr,sizeof(usr))>0)){
			if(strcmp(usr.username,u)==0){
				//sendMsgtoClient(client_fd,"in.\n");
				char *buff = NULL;
				buff = strfromlonglong(usr.accno);
				
				char *bal=(char *)malloc(1000*sizeof(char));
				char *acc_detail=(char *)malloc(100000*sizeof(char));
				
				strcpy(acc_detail,"\n-----------------------------------\nUsername: ");
				strcat(acc_detail, usr.username);
				strcat(acc_detail,"\nAccount number: ");
				strcat(acc_detail,buff);
				strcpy(bal,"\nAvailable Balance: ");
				strcat(bal,printBalance(buff));
				
				sendMsgtoClient(client_fd,strcat(acc_detail,bal));
			}
		}
		close(fd);				
	}
	else sendMsgtoClient(client_fd,"Wrong Username.\n");
}

//----------------------------------------------------------------------------
//User options

void userRequests(char *username,char *password,int client_fd)
{
	int flag=1;
	char option[] = "\n\n-----------------------------------\nWhat do you wish to do?\n1. Check Available Balance\n2. Get Mini Statement\n3. Deposit\n4. WithDraw\n5. Password Change\n6. User Details \nWrite exit for quitting.";
	sendMsgtoClient(client_fd,option);
	
	char *buff=NULL;
	while(flag)
	{
		if(flag == 121){
			flag = 1;
			sendMsgtoClient(client_fd,option);		
		}		
		
		if(buff!=NULL)
			buff=NULL;
		buff=recieveMsgFromClient(client_fd);

		int choice;

		if(strcmp(buff,"exit")==0)
			choice=7;
		else choice=atoi(buff);
		
		char *bal,*str;
		bal=(char *)malloc(1000*sizeof(char));
		str=(char *)malloc(100000*sizeof(char));
		strcpy(bal,"-----------------------------------\nAvailable Balance: ");
		strcpy(str,"-----------------------------------\nMini Statement: \n");
		char *db = accountFromName(username);
		char *acc_detail;
		acc_detail=(char *)malloc(1000*sizeof(char));
		

		char uname[50];
		strcpy(acc_detail,"\n-----------------------------------\nUsername: ");
		strcat(acc_detail, username);
		strcat(acc_detail,"\nAccount Number: ");
		strcat(acc_detail,db);

		switch(choice)
		{
			case 1:	//Check balance
				strcat(bal,printBalance(db));
				sendMsgtoClient(client_fd,strcat(bal,option));
				free(bal);
				break;
			case 2:	//Mini Statement
				strcat(str, printMiniStatement(db,client_fd));
			 	sendMsgtoClient(client_fd,strcat(str,option));
				free(str);
				break;
			case 3:	//Credit amount
				Credit(username, password, client_fd);
				buff=recieveMsgFromClient(client_fd);
				if(strcmp(buff,"no")==0)
					flag = 0;
				else
					flag = 121;
				break;
			case 4:	//Debit
				Debit(username, client_fd);
				buff=recieveMsgFromClient(client_fd);
				if(strcmp(buff,"no")==0)
					flag = 0;
				else
					flag = 121;
				break;
			case 5:	//Change password
				Passwordchng(username, client_fd);
				buff=recieveMsgFromClient(client_fd);
				if(strcmp(buff,"no")==0)
					flag = 0;
				else
					flag = 121;
				break;
			case 6:	//User Details
				sendMsgtoClient(client_fd,strcat(acc_detail,option));
				break;
			case 7:
				flag=0;
				break;
			default:
				sendMsgtoClient(client_fd, "Unknown Query");
				break;
		}
	}
}

//--------------------------------------------------------------
//Admin options

void adminRequests(char *username, int client_fd)
{
	char options[] = "\n-----------------------------------\n1.Create Individual Account\n2.Create Joint Account\n3.Get User Details\n4.Delete User\nWrite exit to quit\n";
	sendMsgtoClient(client_fd, options);	
	char *dat = (char *)malloc(1000*sizeof(char));
	char *name = NULL;
	while(1){	
		char *buff=NULL;
		buff=recieveMsgFromClient(client_fd);	
		if(strcmp(buff,"exit")==0)
			break;
		else{	
			int choice=atoi(buff);
			switch(choice){
			// individual account
			case 1:
				AddUser(client_fd);	
				strcpy(dat, "Account created successfully!\n-----------------------------------\n\n");
				sendMsgtoClient(client_fd, strcat(dat,options));
				break;

			// joint account
			case 2:
				jointAcc(client_fd);
				strcpy(dat,"Joint account created successfully!\n-----------------------------------\n\n");
				sendMsgtoClient(client_fd,strcat(dat,options));
				break;

			// get user details
			case 3:
				sendMsgtoClient(client_fd,"Enter username: ");
				name = NULL;
				name = recieveMsgFromClient(client_fd);
				userDetails(client_fd,name);
				strcpy(dat,"\n-----------------------------------\n\n");
				sendMsgtoClient(client_fd,strcat(dat,options));
				break;

			// delete user
			case 4:
				sendMsgtoClient(client_fd,"Enter the Username: ");
				char *buff=NULL;
				buff=recieveMsgFromClient(client_fd);
				deleteUser(client_fd,buff);
				sendMsgtoClient(client_fd,options);
				break;

			default:
				strcpy(dat, "Invalid Choice"); 
				sendMsgtoClient(client_fd, strcat(dat,options));
				break;
			}	
		}
	}
}

//---------------------------------------------------------------
//Get credentials for login

void getupcli(char *username,char *password,int client_fd)
{
	char *ruser,*rpass;
	sendMsgtoClient(client_fd,"Enter Username: ");
	ruser=recieveMsgFromClient(client_fd);

	sendMsgtoClient(client_fd,"Enter Password: ");
	rpass=recieveMsgFromClient(client_fd);

	int i=0;
	while(ruser[i]!='\0' && ruser[i]!='\n')
	{
		username[i]=ruser[i];
		i++;
	}

	username[i]='\0';

	i=0;
	while(rpass[i]!='\0' && rpass[i]!='\n')
	{
		password[i]=rpass[i];
		i++;
	}
	password[i]='\0';

}

//--------------------------------------------------------------
//Authorize access

int authorize(char* username,char *password)
{
	printf("Authorizing\n");
    ssize_t readc;
	user usr;
	int fd = open("login.dat", O_RDONLY);
	while((readc = read(fd, &usr, sizeof(usr))) > 0){
		if(strcmp(usr.username,username)==0){
			if(strcmp(usr.password,password)==0){		
                if(usr.type=='C' || usr.type == 'J'){	
					// active account
					if(usr.active=='y'){
						//printf("\naccepting");
						close(fd);
                    				return USER;
					}
					else
					{
						//printf("rejecting");
						close(fd);
						return UNAUTH_USER;
					}
		    	}
                else if(usr.type =='A'){
                    close(fd);
                    return ADMIN;
                }
            }
        }
    }

    close(fd);
	return UNAUTH_USER;
}

//---------------------------------------------------------------------------
//Get the user

void menuClient(int client_fd)
{
	char *username,*password;
	username=(char *)malloc(100);
	password=(char *)malloc(100);
	int utype;
	
	getupcli(username,password,client_fd); // get username and password from the user
	utype=authorize(username,password); // validate creds and account (whether or not it is active)

	char *str=(char *)malloc(sizeof(char)*60);
	strcpy(str,"Thanks ");

	switch(utype)
	{
		case USER:
			printf("Welcome %s\n",username);
			//sendMsgtoClient(client_fd, "\n-------------------WELCOME USER--------------------\n");
			userRequests(username,password,client_fd);
			closeclient(client_fd,strcat(str,username));
			break;
		case ADMIN:
			printf("Welcome admin %s\n",username);
			//sendMsgtoClient(client_fd, "\n-------------------WELCOME ADMIN--------------------\n");
			adminRequests(username, client_fd);
			closeclient(client_fd,strcat(str,username));
			break;	
		
		case UNAUTH_USER:
			closeclient(client_fd,"Unauthorised access");
			break;
		default:
			closeclient(client_fd,"Unauthorised access");
			break;
	}
}

//--------------------------------------------------------------------------
//Close connection

void closeclient(int client_fd,char *str)
{
	sendMsgtoClient(client_fd, str);
    shutdown(client_fd, SHUT_RDWR);
}

//----------------------------------------------------------------------------
//Driver Function

int main()
{
	int sock_fd,client_fd,port_no;
	struct sockaddr_in serv_addr, cli_addr;

	memset((void*)&serv_addr, 0, sizeof(serv_addr));
	port_no=8090;

	sock_fd=socket(AF_INET, SOCK_STREAM, 0);

	serv_addr.sin_port = htons(port_no);         //set the port number
	serv_addr.sin_family = AF_INET;             //setting DOMAIN
	serv_addr.sin_addr.s_addr = INADDR_ANY;     //permits any incoming IP

	if(bind(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
	    printf("Error on binding.\n");
	    exit(EXIT_FAILURE);
	}
	
	int reuse=1;
	
	//int setsockopt(int sockfd, int level, int optname,const void *optval, socklen_t optlen);
	//setsockopt() manipulate options for the socket referred to by the file descriptor sockfd.
	
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
	
	listen(sock_fd, 5); 
	int clisize=sizeof(cli_addr);

	while(1) {
	    memset(&cli_addr, 0, sizeof(cli_addr));
	    if((client_fd = accept(sock_fd, (struct sockaddr*)&cli_addr, &clisize)) < 0) {
	        printf("Error on accept.\n");
	        exit(EXIT_FAILURE);
	    }
	    //creating a child to serve the client
	    switch(fork()) {
	        case -1:
	            printf("Error in fork.\n");
	            break;
	        case 0: {
	            close(sock_fd);
	            menuClient(client_fd); //Main function for service to client
	            exit(EXIT_SUCCESS);
	            break;
	        }
	        default:
	            close(client_fd);
	            break;
	    }
	}
}
