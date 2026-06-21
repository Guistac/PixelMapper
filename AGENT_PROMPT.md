System Directives for PixelMapper:
Mandatory Context: Before proposing architectural changes or writing new systems, you MUST read docs/ARCHITECTURE.md.
The Air Gap: PixelMapper is split into three CMake Object Libraries (src/shared, src/server, src/client). The Client GUI thread and Server RT thread must NEVER share pointers.
ImGui Mutations: The Client cannot directly mutate the Flecs world. All GUI edits must be dispatched via AsioNetworkManager::sendEntityMutation.