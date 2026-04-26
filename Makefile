# Query Optimizer - Makefile

CXX      = g++
CC       = gcc
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I.
CFLAGS   = -Wall -g -I.
LDFLAGS  = -lws2_32

PROG     = query_optimizer

C_OBJS   = lex.yy.o y.tab.o
CXX_OBJS = src/main.o src/catalog.o src/logical_plan.o src/physical_plan.o src/heuristic_optimizer.o src/cost_estimator.o src/plan_enumerator.o src/optimizer.o src/http_server.o

all: $(PROG)

$(PROG): $(C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

lex.yy.c: sql_lexer.l y.tab.h sql_parser.h
	flex sql_lexer.l

y.tab.c y.tab.h: sql_parser.y sql_parser.h
	bison -d -o y.tab.c sql_parser.y

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -c $< -o $@

y.tab.o: y.tab.c sql_parser.h
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	-del /Q $(PROG).exe lex.yy.c lex.yy.o y.tab.c y.tab.h y.tab.o 2>nul
	-del /Q src\*.o 2>nul

.PHONY: all clean
