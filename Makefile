TRANS ?= /Applications/Monkey/bin/transcc_macos
CONFIG ?= debug
IMPORT = wizard/commands/commands.monkey

#
# CLEANING
#

clean: ## Remove build artifacts
	echo "> Cleaning ..."
	rm -rf src.build/
	rm ${IMPORT}

#
# BUILDING
#

build: clean commandsimport ## Build monkey-wizard
	echo "> Building ..."
	${TRANS} \
		-config=${CONFIG} \
		-target=C++_Tool \
		-builddir=src.build \
		-build \
		wizard.monkey

commandsimport:
	echo "> Detecting commands ..."
	echo "Strict" > ${IMPORT}
	echo "" >> ${IMPORT}
	echo "Public" >> ${IMPORT}
	echo "" >> ${IMPORT}
	for module in $$(ls -1 wizard/commands/ | grep -v "commands.monkey" | egrep --color=none -o "^[^\.]+"); do \
		echo "Import $${module}" >> ${IMPORT}; \
	done

#
# MAKEFILE
#

help:
	echo "Available commands:"
	grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[35m%-20s\033[0m %s\n", $$1, $$2}'

.DEFAULT_GOAL = help

.PHONY: clean build commandsimport

.SILENT:
