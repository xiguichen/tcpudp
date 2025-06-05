compile:
	cd Build && cmake -G Ninja ../src && ninja

test: compile
ifeq ($(OS),Windows_NT)
	test.cmd
else
	cd Build && tests/CommonTest
endif


run: compile
ifeq ($(OS),Windows_NT)
	run.cmd
else
	./run.sh
endif

