#pragma once

namespace Pomme
{
	// Throw this exception to interrupt the game's main loop
	class QuitRequest : public std::exception
	{
	public:
		virtual const char* what() const noexcept;
	};

	struct InitParams
	{
		const char* windowName;
		int windowWidth;
		int windowHeight;
		int msaaSamples;
	};

	void Init(const InitParams& params);

	void Shutdown();
}

