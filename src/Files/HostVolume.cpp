#include "PommeEnums.h"
#include "PommeDebug.h"
#include "PommeFiles.h"
#include "Files/HostVolume.h"
#include "Utilities/bigendianstreams.h"
#include "Utilities/StringUtils.h"

#include <fstream>
#include <iostream>

#define LOG POMME_GENLOG(POMME_DEBUG_FILES, "HOST")

using namespace Pomme;
using namespace Pomme::Files;

struct HostForkHandle : public ForkHandle
{
	std::fstream backingStream;

public:
	HostForkHandle(ForkType theForkType, char perm, fs::path& path, const FSSpec& theSpec)
		: ForkHandle(theForkType, perm, theSpec)
	{
		std::ios::openmode openmode = std::ios::binary;
		if (permission & fsWrPerm) openmode |= std::ios::out;
		if (permission & fsRdPerm) openmode |= std::ios::in;

		backingStream = std::fstream(path, openmode);
	}

	virtual ~HostForkHandle() = default;

	virtual std::iostream& GetStream() override
	{
		return backingStream;
	}
};


HostVolume::HostVolume(short vRefNum)
	: Volume(vRefNum)
{
	// default directory (ID 0)
	directories.push_back(fs::current_path());
}

//-----------------------------------------------------------------------------
// Public utilities

long HostVolume::GetDirectoryID(const fs::path& dirPath)
{
	if (fs::exists(dirPath) && !fs::is_directory(dirPath))
	{
		std::cerr << "Warning: GetDirectoryID should only be used on directories! " << dirPath << "\n";
	}

	auto it = std::find(directories.begin(), directories.end(), dirPath);
	if (it != directories.end())
	{
		return std::distance(directories.begin(), it);
	}

	directories.emplace_back(dirPath);
	LOG << "directory " << directories.size() - 1 << ": " << dirPath << "\n";
	return (long) directories.size() - 1;
}

//-----------------------------------------------------------------------------
// Internal utilities

fs::path HostVolume::ToPath(long parID, const char* name)
{
	return ToPath(parID, u8string((const char8_t*)name));
}

fs::path HostVolume::ToPath(long parID, const u8string& name)
{
	fs::path path = directories.at(parID);
	path /= name;
	return path.lexically_normal();
}

FSSpec HostVolume::ToFSSpec(const fs::path& fullPath)
{
	auto parentPath = fullPath;
	parentPath.remove_filename();

	FSSpec spec;
	spec.vRefNum = volumeID;
	spec.parID = GetDirectoryID(parentPath);
	snprintf(spec.cName, 256, "%s", (const char*) fullPath.filename().u8string().c_str());
	return spec;
}

static void ADFJumpToResourceFork(std::istream& stream)
{
	auto f = Pomme::BigEndianIStream(stream);

	if (0x0005160700020000ULL != f.Read<UInt64>())
	{
		throw std::runtime_error("No ADF magic");
	}
	f.Skip(16);
	auto numOfEntries = f.Read<UInt16>();

	for (int i = 0; i < numOfEntries; i++)
	{
		auto entryID = f.Read<UInt32>();
		auto offset = f.Read<UInt32>();
		f.Skip(4); // length
		if (entryID == 2)
		{
			// Found entry ID 2 (resource fork)
			f.Goto(offset);
			return;
		}
	}

	throw std::runtime_error("Didn't find entry ID=2 in ADF");
}

OSErr HostVolume::OpenFork(const FSSpec* spec, ForkType forkType, char permission, std::unique_ptr<ForkHandle>& handle)
{
	if (permission == fsCurPerm)
	{
		// TODO: fsCurPerm not implemented yet
		return unimpErr;
	}

	if ((permission & fsWrPerm) && forkType != ForkType::DataFork)
	{
		// TODO: opening resource fork for writing isn't implemented yet
		return unimpErr;
	}

	auto path = ToPath(spec->parID, spec->cName);

	if (forkType == DataFork)
	{
		if (!fs::is_regular_file(path))
		{
			return fnfErr;
		}
		handle = std::make_unique<HostForkHandle>(DataFork, permission, path, *spec);
	}
	else
	{
		// We want to open a resource fork on the host volume. It is likely stored as <NAME>.rsrc.
		path += ".rsrc";
		if (!fs::is_regular_file(path))
		{
			return fnfErr;
		}
		handle = std::make_unique<HostForkHandle>(ResourceFork, permission, path, *spec);
		if (!handle->GetStream().good())
		{
			return ioErr;
		}
		ADFJumpToResourceFork(handle->GetStream());
	}

	if (!handle->GetStream().good())
	{
		return ioErr;
	}

	return noErr;
}

static bool CaseInsensitiveAppendToPath(fs::path& path, const u8string& element, bool skipFiles = false)
{
	fs::path naiveConcat = path / element;

	if (!fs::exists(path))
	{
		path = naiveConcat;
		return false;
	}

	if (fs::exists(naiveConcat))
	{
		path = naiveConcat;
		return true;
	}

#if POMME_CASE_SENSITIVE_FSSPEC
	if (!skipFiles)
	{
		fs::path candidateResourcePath = naiveConcat;
		candidateResourcePath += ".rsrc";
		if (fs::is_regular_file(candidateResourcePath))
		{
			path = naiveConcat;
			return true;
		}
	}
#else
	// Convert path element to uppercase for case-insensitive comparisons
	const auto uppercaseElement = UppercaseCopy(element);

	for (const auto& candidate : fs::directory_iterator(path))
	{
		if (skipFiles && !candidate.is_directory())
		{
			continue;
		}

		fs::path candidateFilename = candidate.path().filename();

		// It might be an AppleDouble resource fork ("file.rsrc")
		if (candidate.is_regular_file() && candidateFilename.extension() == ".rsrc")
		{
			candidateFilename.replace_extension("");
		}

		// Convert candidate filename to uppercase for case-insensitive comparison
		const u8string uppercaseCandidateFilename = UppercaseCopy(candidateFilename.u8string());

		if (uppercaseElement == uppercaseCandidateFilename)
		{
			path /= candidateFilename;
			return true;
		}
	}
#endif

	path = naiveConcat;
	return false;
}

//-----------------------------------------------------------------------------
// Implementation

OSErr HostVolume::FSMakeFSSpec(long dirID, const u8string& fileName, FSSpec* spec)
{
	if (dirID < 0 || (unsigned long) dirID >= directories.size())
	{
		throw std::runtime_error("HostVolume::FSMakeFSSpec: directory ID not registered.");
	}

	auto path = directories.at(dirID);
	auto suffix = fileName;

	// Case-insensitive sanitization
	bool exists = fs::exists(path);
	u8string::size_type begin = (suffix.at(0) == ':') ? 1 : 0;

	// Iterate on path elements between colons
	while (begin < suffix.length())
	{
		auto end = suffix.find(':', begin);

		bool isLeaf = end == std::string::npos; // no ':' found => end of path
		if (isLeaf) end = suffix.length();

		if (end == begin) // "::" => parent directory
		{
			path = path.parent_path();
		}
		else
		{
			auto element = suffix.substr(begin, end - begin);
			exists = CaseInsensitiveAppendToPath(path, element, !isLeaf);
		}

		// +1: jump over current colon
		begin = end + 1;
	}

	path = path.lexically_normal();

	LOG << path << "\n";

	*spec = ToFSSpec(path);

	return exists ? noErr : fnfErr;
}

OSErr HostVolume::DirCreate(long parentDirID, const u8string& directoryName, long* createdDirID)
{
	const auto path = ToPath(parentDirID, directoryName);

	if (fs::exists(path))
	{
		if (fs::is_directory(path))
		{
			LOG << __func__ << ": directory already exists: " << path << "\n";
			return noErr;
		}
		else
		{
			std::cerr << __func__ << ": a file with the same name already exists: " << path << "\n";
			return bdNamErr;
		}
	}

	try
	{
		fs::create_directory(path);
	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << __func__ << " threw " << e.what() << "\n";
		return ioErr;
	}

	if (createdDirID)
	{
		*createdDirID = GetDirectoryID(path);
	}

	LOG << __func__ << ": created " << path << "\n";
	return noErr;
}

OSErr HostVolume::FSpCreate(const FSSpec* spec, OSType creator, OSType fileType, ScriptCode scriptTag)
{
	(void) creator;
	(void) fileType;
	(void) scriptTag;

	std::ofstream df(ToPath(spec->parID, spec->cName));
	df.close();
	// TODO: we could write an AppleDouble file to save the creator/filetype.
	return noErr;
}

OSErr HostVolume::FSpDelete(const FSSpec* spec)
{
	auto path = ToPath(spec->parID, spec->cName);

	if (fs::remove(path))
		return noErr;
	else
		return fnfErr;
}
