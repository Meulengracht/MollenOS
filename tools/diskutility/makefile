#Change this to the depth of the project folders
#if needed, add a preffix for a common project folder
SOURCE = $(wildcard mfstool/*/*/*.cs mfstool/*/*.cs mfstool/*.cs)

all: ../../diskutility

../../diskutility: $(SOURCE)
	@printf "%b" "\033[0;36mCreating tool " $@ "\033[m\n"
	@mcs $(SOURCE) /reference:System.Management.dll /reference:DiscUtils.dll -out:$@
	@cp *.dll ../../

clean:
	@rm -f ../../diskutility
	@rm -f ../../*.dll