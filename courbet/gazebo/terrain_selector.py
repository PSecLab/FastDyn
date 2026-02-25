import sys

def main():
    '''
    Get terrain from user and generate rover_sdf_text with selected terrain.

    Options:
    - rocky_terrain
    - sandy_terrain
    - icy_plane
    - runway
    '''
    # fail if more than one cli argument is provided
    if len(sys.argv) != 2:
        print("Usage: python terrain_selector.py <terrain_name>")
        print("Available terrains: rocky_terrain, sandy_terrain, icy_plane, runway")
        sys.exit(1)

    terrain = sys.argv[1]

    pose = "-29 545 0 0 0 363"  # default pose
    if terrain != "runway":
        pose = "0 0 0 0 0 0"
    rover_sdf_text = f"""<?xml version="1.0" ?>
<sdf version="1.7">
  <world name="runway">
    <gravity>0 0 -9.8000000000000007</gravity>
    <!-- <magnetic_field>6.0000000000000002e-06 2.3e-05 -4.1999999999999998e-05</magnetic_field> -->
    <magnetic_field>2.5e-05 1.0e-06 4.0e-05</magnetic_field>
    <atmosphere type='adiabatic'>
      <temperature>288.15</temperature>    <!-- Kelvin (~15 °C) -->
      <pressure>101325</pressure>         <!-- Pascals (sea level standard) -->
    </atmosphere>
    <spherical_coordinates>
      <surface_model>EARTH_WGS84</surface_model>
      <latitude_deg>-35.3632623</latitude_deg>
      <longitude_deg>149.165237</longitude_deg>
      <elevation>594.1</elevation>
      <heading_deg>0</heading_deg>
    </spherical_coordinates>
    <physics name="1ms" type="ignored">
      <max_step_size>0.001</max_step_size>
      <real_time_factor>0</real_time_factor>
    </physics>
    <plugin filename="gz-sim-physics-system"
      name="gz::sim::systems::Physics">
    </plugin>
    <plugin filename="gz-sim-user-commands-system"
      name="gz::sim::systems::UserCommands">
    </plugin>
    <plugin filename="gz-sim-scene-broadcaster-system"
      name="gz::sim::systems::SceneBroadcaster">
    </plugin>
    <plugin filename="gz-sim-imu-system"
      name="gz::sim::systems::Imu">
    </plugin>
    <plugin filename="gz-sim-magnetometer-system"
      name="gz::sim::systems::Magnetometer">
    </plugin>
    <plugin name='gz::sim::systems::NavSat' filename='gz-sim-navsat-system'/>
    <plugin filename="gz-sim-air-pressure-system"
      name="gz::sim::systems::AirPressure">
      <topic>/world/runway/air_pressure</topic>
    </plugin>

    <wind>
      <linear_velocity>0 0 0</linear_velocity>
    </wind>

    <plugin
      filename="libgz-sim8-wind-effects-system.so"
      name="gz::sim::systems::WindEffects">
      <horizontal>
        <magnitude>
          <time_for_rise>10</time_for_rise>
          <sin>
            <amplitude_percent>0.05</amplitude_percent>
            <period>60</period>
          </sin>
          <noise type="gaussian">
          <mean>0</mean>
          <stddev>0.0002</stddev>
          </noise>
        </magnitude>
        <direction>
          <time_for_rise>30</time_for_rise>
          <sin>
            <amplitude>5</amplitude>
            <period>20</period>
          </sin>
          <noise type="gaussian">
          <mean>0</mean>
          <stddev>0.03</stddev>
          </noise>
        </direction>
      </horizontal>
      <vertical>
        <noise type="gaussian">
        <mean>0</mean>
        <stddev>0.03</stddev>
        </noise>
      </vertical>
    </plugin>

    <scene>
      <ambient>1.0 1.0 1.0</ambient>
      <background>0.8 0.8 0.8</background>
      <sky></sky>
    </scene>

    <light type="directional" name="sun">
      <cast_shadows>true</cast_shadows>
      <pose>0 0 10 0 0 0</pose>
      <diffuse>0.8 0.8 0.8 1</diffuse>
      <specular>0.8 0.8 0.8 1</specular>
      <attenuation>
        <range>1000</range>
        <constant>0.9</constant>
        <linear>0.01</linear>
        <quadratic>0.001</quadratic>
      </attenuation>
      <direction>-0.5 0.1 -0.9</direction>
    </light>

    <include>
      <pose degrees="true">{pose}</pose>
      <uri>model://{terrain}</uri>
    </include>


    <!-- Ramp -->
    <model name="ramp">
      <static>true</static>

      <!-- Position in world (adjust X/Y to put it on the runway) -->
      <pose>-40 0 0.15 0 0 177</pose> <!-- x y z roll pitch yaw -->

      <link name="link">
        <collision name="collision">
          <geometry>
            <box>
              <size>16 10 0.3</size> <!-- length width thickness -->
            </box>
          </geometry>

          <!-- Traction matters a LOT for rovers -->
          <surface>
            <friction>
              <ode>
                <mu>1.3</mu>
                <mu2>1.3</mu2>
              </ode>
            </friction>
          </surface>
        </collision>

        <visual name="visual">
          <geometry>
            <box>
              <size>6 3 0.3</size>
            </box>
          </geometry>
          <material>
            <ambient>0.4 0.4 0.4 1</ambient>
            <diffuse>0.6 0.6 0.6 1</diffuse>
          </material>
        </visual>

        <!-- Incline angle: ~15 degrees -->
        <pose>0 0 0 0 0.26 0</pose>
      </link>
    </model>
    <!-- Rough terrain -->
    <!-- <include>
      <pose>2 0 -0.12 0 0 0</pose>
      <uri>https://fuel.gazebosim.org/1.0/OpenRobotics/models/FRC 2016 Rough Terrain</uri>
      <name>rough_terrain_1</name>
    </include>
    <include>
      <pose>2 0.35 -0.12 0 0 0</pose>
      <uri>https://fuel.gazebosim.org/1.0/OpenRobotics/models/FRC 2016 Rough Terrain</uri>
      <name>rough_terrain_2</name>
    </include>
    <include>
      <pose>2 -0.35 -0.12 0 0 0</pose>
      <uri>https://fuel.gazebosim.org/1.0/OpenRobotics/models/FRC 2016 Rough Terrain</uri>
      <name>rough_terrain_3</name>
    </include>
    <include>
      <pose>2 0.7 -0.12 0 0 0</pose>
      <uri>https://fuel.gazebosim.org/1.0/OpenRobotics/models/FRC 2016 Rough Terrain</uri>
      <name>rough_terrain_4</name>
    </include>
    <include>
      <pose>2 -0.7 -0.12 0 0 0</pose>
      <uri>https://fuel.gazebosim.org/1.0/OpenRobotics/models/FRC 2016 Rough Terrain</uri>
      <name>rough_terrain_5</name>
    </include> -->

    <!-- Rover -->
    <include>
      <pose degrees="true">0 0 50 0 0 90</pose>
      <!-- <uri>model://gs_drone</uri> -->
      <uri>model://r1_rover</uri>
      <!-- <uri>model://mini_talon_vtail</uri> -->
    </include>

  </world>
</sdf>
"""
    # write rover_sdf_text to rover_sdf_file
    # Hard Coded
    rover_sdf_file_path = "/root/rooney/SITL_Models/Gazebo/worlds/r1_rover_runway.sdf"
    with open(rover_sdf_file_path, "w") as rover_sdf_file:
        rover_sdf_file.write(rover_sdf_text)

if __name__ == "__main__":
    main()

