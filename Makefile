WORKDIR = "D:\repos\myWork\schmal"
INC = /I D:\repos\rapidjson\include /I D:\repos\asio\asio\include /I D:\repos\openssl\include \
		/I D:\repos\myWork\schmal
CFLAGS = /await /Od /Zi /EHsc /D "WIN32" /D ASIO_STANDALONE /D ASIO_HAS_STD_ADDRESSOF /D ASIO_HAS_STD_ARRAY \
		/D ASIO_HAS_CSTDINT /D ASIO_HAS_STD_SHARED_PTR /D ASIO_HAS_STD_TYPE_TRAITS \
		/D ASIO_HAS_VARIADIC_TEMPLATES /D ASIO_HAS_STD_FUNCTION /D ASIO_HAS_STD_CHRONO /D BOOST_ALL_NO_LIB \
		/D _WIN32_WINNT=0x0501 /D _WINSOCK_DEPRECATED_NO_WARNINGS
LFLAGS = /OUT:schmal.exe /MACHINE:X86 /DYNAMICBASE "libssl.lib" "libcrypto.lib" /LIBPATH:"D:\repos\openssl" \
		/TLBID:1 
SOURCES = D:\repos\myWork\schmal\schmal.cpp

all: clean build

build: 
	cd $(WORKDIR)\build
	cl $(CFLAGS) $(INC) $(SOURCES)  /link $(LFLAGS)
	copy D:\repos\myWork\schmal\build\schmal.exe D:\repos\myWork\schmal\bin\schmal.exe
	
clean:
	cd $(WORKDIR)\build
	del .