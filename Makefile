WORKDIR = "D:\repos\myWork\schmal"
INC = /I D:\repos\rapidjson\include /I D:\repos\myWork\schmal
CFLAGS = /await /Od /Zi /EHsc 
LFLAGS = /OUT:schmal.exe
SOURCES = D:\repos\myWork\schmal\main.cpp D:\repos\myWork\schmal\schmal.cpp

all: clean build

build: 
	cd $(WORKDIR)\build
	cl $(CFLAGS) $(INC) $(SOURCES)  /link $(LFLAGS)
clean:
	cd $(WORKDIR)\build
	del .