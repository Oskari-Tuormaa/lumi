target_sources(app PRIVATE ./main.cpp ./render_engine.cpp)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED CONFIG_BOARD_NATIVE_SIM_NATIVE_64)
    target_sources(app PRIVATE ./display_sim.cpp)
else()
    target_sources(app PRIVATE ./display_target.cpp)
endif()
