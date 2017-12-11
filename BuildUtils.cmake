if(PLATFORM_WIN32 OR PLATFORM_UNVIRSAL_WINDOWS)
	
	function(copy_required_dlls TARGET_NAME)
		set(ENGINE_DLLS 
			GraphicsEngineD3D11-shared 
			GraphicsEngineD3D12-shared 
		)
		if(PLATFORM_WIN32)
			list(APPEND ENGINE_DLLS GraphicsEngineOpenGL-shared)
		endif()

		foreach(DLL ${ENGINE_DLLS})
			add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"\"$<TARGET_FILE:${DLL}>\""
					"\"$<TARGET_FILE_DIR:${TARGET_NAME}>\"")
		endforeach(DLL)

		# Copy D3Dcompiler_47.dll 
		if(MSVC)
			if(WIN64)
				set(D3D_COMPILER_PATH "\"$(VC_ExecutablePath_x64_x64)\\D3Dcompiler_47.dll\"")
			else()
				set(D3D_COMPILER_PATH "\"$(VC_ExecutablePath_x86_x86)\\D3Dcompiler_47.dll\"")
			endif()
			add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					${D3D_COMPILER_PATH}
					"\"$<TARGET_FILE_DIR:${TARGET_NAME}>\"")
		endif()
	endfunction()

	
	function(set_header_file_only_property FILES)
		foreach(FILE ${FILES})
			set_source_files_properties(
				${FILE} PROPERTIES HEADER_FILE_ONLY TRUE
			)
		endforeach(FILE)
	endfunction()

endif(PLATFORM_WIN32 OR PLATFORM_UNVIRSAL_WINDOWS)
