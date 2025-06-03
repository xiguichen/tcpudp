compile:
	cd Build && cmake -G Ninja ../src && ninja

test: compile
	cd Build && tests/CommonTest
