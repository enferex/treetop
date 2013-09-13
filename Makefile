APP = treetop
SRC = main.c
CFLAGS = -g3 -Wall
LIBS = -lcurses -lmenu -lpanel

$(APP): $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(LIBS) -o $(APP)

test: $(APP)
	./$(APP) test.config

debug: $(APP)
	exec gdb --args ./$(APP) test.config

clean:
	rm -fv $(APP)
