TRANS=/Applications/Monkey/bin/trans_macos
CONFIG=debug
IMPORT=wizard/commands/commands.monkey

all: commandsimport build run

build:
	${TRANS} -config=${CONFIG} -target=StdCpp -build wizard.monkey

run:
	./wizard.build/cpptool/main_macos ${ARGS}

commandsimport:
	echo "Strict" > ${IMPORT}
	echo "" >> ${IMPORT}
	echo "Public" >> ${IMPORT}
	echo "" >> ${IMPORT}

	for module in $$(ls -1 wizard/commands/ | grep -v "commands.monkey" | egrep -o "^[^\.]+"); do \
		echo "Import $${module}" >> ${IMPORT}; \
	done

.PHONY: all build run commandsimport
