compile:
	cd ../Build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../src && ninja

test: compile
ifeq ($(OS),Windows_NT)
	cd .. && test.cmd
else
	cd ../Build && tests/CommonTest
endif


run: compile
ifeq ($(OS),Windows_NT)
	../run.cmd
else
	../run.sh
endif

