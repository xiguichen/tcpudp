.PHONY: all clean


build:
	cd Build && cmake -G Ninja ../src && ninja 
test: build
	cd Build && tests/CommonTest
