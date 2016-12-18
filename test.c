#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <fcntl.h>

#define FILE_SIZE 1024

int read_line(char* buf, char* file_buf, int* cursor);

int main(void){
    char open_path[] = "./test/input1.txt";
    char buf[200];
    char file_buf[FILE_SIZE];
    int read_fd;
    int length;
    int cursor = 0;
    
    if((read_fd = open(open_path, O_RDONLY)) == -1){
        perror("open error. put read_path");
    }
    if(read(read_fd, file_buf, FILE_SIZE) == -1){
        perror("read error");
    }
    
    while((length = read_line(buf, file_buf, &cursor)) > 0){
        printf("length: %d\n", length);
        printf("strlen: %lu\n", strlen(buf));
        printf("%s", buf);
    }
}
int read_line(char* buf, char* file_buf, int* cursor)
{
    int ch, count = 0;
    
    if(file_buf[*cursor] == '\n' || file_buf[*cursor] == '\0')
        return -1;
    
    do{
        ch = file_buf[*cursor+count];
        buf[count] = ch;
        count++;
    }while(ch != '\n' && ch!='\0');
    *cursor += count;
    buf[count] = '\0';
    return count;
}
