# 고급 시스템 프로그래밍 Advanced System Programming

국민대학교 컴퓨터공학부 3학년 20113313 이창현
 
File 구성
- chat.c  : 클라이언트와 서버간 파일을 주고받는 메인 소스.
- myqueue.c : queue 자료구조를 사용하기 위한 소스. 출처:http://ehclub.net/58
- gen.c   : n 개의  m MB의 크기의 text 파일 만들기 (client가 전송할 파일

컴파일 및 실행방법

gcc chat.c -o chat -fopenmp
서버실행: ./chat s
client 실행: ./chat c 1~2

