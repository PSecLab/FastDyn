use std::f64::consts::PI;

const MASS: f64 = 2.0;
const GRAVITY: f64 = 9.806_65;
const CT: f64 = 8.548_58e-6;
const CM: f64 = 0.016;
const ARM: f64 = 0.25 * std::f64::consts::FRAC_1_SQRT_2;
const OMEGA_MAX: f64 = 1_100.0;
const MOTOR_TAU: f64 = 0.018;
const INERTIA: [f64; 3] = [0.021_666_666_666_7, 0.021_666_666_666_7, 0.04];

fn add(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn scale(a: [f64; 3], s: f64) -> [f64; 3] {
    [a[0] * s, a[1] * s, a[2] * s]
}

fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn rotate(q: [f64; 4], v: [f64; 3]) -> [f64; 3] {
    let qv = [q[1], q[2], q[3]];
    let t = scale(cross(qv, v), 2.0);
    add(v, add(scale(t, q[0]), cross(qv, t)))
}

#[derive(Clone, Debug)]
pub struct Quadrotor {
    pub position: [f64; 3],
    pub velocity: [f64; 3],
    pub quaternion: [f64; 4],
    pub body_rate: [f64; 3],
    motor_omega: [f64; 4],
    specific_force: [f64; 3],
}

impl Default for Quadrotor {
    fn default() -> Self {
        Self {
            position: [0.0; 3],
            velocity: [0.0; 3],
            quaternion: [1.0, 0.0, 0.0, 0.0],
            body_rate: [0.0; 3],
            motor_omega: [0.0; 4],
            specific_force: [0.0, 0.0, GRAVITY],
        }
    }
}

impl Quadrotor {
    pub fn altitude(&self) -> f64 {
        self.position[2]
    }

    pub fn vertical_speed(&self) -> f64 {
        self.velocity[2]
    }

    pub fn euler(&self) -> [f64; 3] {
        let [w, x, y, z] = self.quaternion;
        [
            (2.0 * (w * x + y * z)).atan2(1.0 - 2.0 * (x * x + y * y)),
            (2.0 * (w * y - z * x)).clamp(-1.0, 1.0).asin(),
            (2.0 * (w * z + x * y)).atan2(1.0 - 2.0 * (y * y + z * z)),
        ]
    }

    /// IMU sample in the FLU body frame that this plant integrates in and
    /// that synapse_fbs v0.5 standardizes for inertial vectors.
    pub fn imu_flu(&self) -> ([f32; 3], [f32; 3]) {
        let flu = |v: [f64; 3]| [v[0] as f32, v[1] as f32, v[2] as f32];
        (flu(self.body_rate), flu(self.specific_force))
    }

    pub fn step(&mut self, motor: [f32; 4], dt: f64) {
        // Small fixed substeps keep the plant stable while one Synapse exchange
        // advances several 1600 Hz firmware control ticks.
        let substeps = (dt / 0.001).ceil().max(1.0) as usize;
        let h = dt / substeps as f64;
        for _ in 0..substeps {
            self.substep(motor, h);
        }
    }

    fn substep(&mut self, motor: [f32; 4], dt: f64) {
        for (omega, command) in self.motor_omega.iter_mut().zip(motor) {
            let target = f64::from(command.clamp(0.0, 1.0)) * OMEGA_MAX;
            *omega += (target - *omega) * (1.0 - (-dt / MOTOR_TAU).exp());
        }

        let thrust = self.motor_omega.map(|omega| CT * omega * omega);
        let total_thrust = thrust.iter().sum::<f64>();
        let moment = [
            ARM * (-thrust[0] - thrust[1] + thrust[2] + thrust[3]),
            ARM * (-thrust[0] + thrust[1] + thrust[2] - thrust[3]),
            CM * (-thrust[0] + thrust[1] - thrust[2] + thrust[3]),
        ];
        let angular_momentum = [
            INERTIA[0] * self.body_rate[0],
            INERTIA[1] * self.body_rate[1],
            INERTIA[2] * self.body_rate[2],
        ];
        let gyroscopic = cross(self.body_rate, angular_momentum);
        for axis in 0..3 {
            let damping = [0.035, 0.035, 0.025][axis] * self.body_rate[axis];
            self.body_rate[axis] +=
                (moment[axis] - gyroscopic[axis] - damping) / INERTIA[axis] * dt;
        }

        let [w, x, y, z] = self.quaternion;
        let [p, q, r] = self.body_rate;
        self.quaternion[0] += 0.5 * (-x * p - y * q - z * r) * dt;
        self.quaternion[1] += 0.5 * (w * p + y * r - z * q) * dt;
        self.quaternion[2] += 0.5 * (w * q - x * r + z * p) * dt;
        self.quaternion[3] += 0.5 * (w * r + x * q - y * p) * dt;
        let norm = self.quaternion.iter().map(|v| v * v).sum::<f64>().sqrt();
        for value in &mut self.quaternion {
            *value /= norm;
        }

        let drag_body = scale(self.body_rate, 0.0); // leaves an explicit hook for aero moments
        let body_force = add([0.0, 0.0, total_thrust], drag_body);
        self.specific_force = scale(body_force, 1.0 / MASS);
        let world_force = rotate(self.quaternion, body_force);
        let linear_drag = scale(self.velocity, -0.18);
        let acceleration = add(
            scale(add(world_force, linear_drag), 1.0 / MASS),
            [0.0, 0.0, -GRAVITY],
        );
        self.velocity = add(self.velocity, scale(acceleration, dt));
        self.position = add(self.position, scale(self.velocity, dt));

        if self.position[2] < 0.0 {
            self.position[2] = 0.0;
            if self.velocity[2] < 0.0 {
                self.velocity[2] = 0.0;
            }
            self.velocity[0] *= (-8.0 * dt).exp();
            self.velocity[1] *= (-8.0 * dt).exp();
            if total_thrust < MASS * GRAVITY {
                self.specific_force = [0.0, 0.0, GRAVITY];
            }
        }
    }
}

pub fn radians_to_degrees(value: f64) -> f64 {
    value * 180.0 / PI
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn disarmed_vehicle_stays_on_ground_with_level_imu() {
        let mut plant = Quadrotor::default();
        for _ in 0..1_000 {
            plant.step([0.0; 4], 0.001);
        }
        let (_, accel) = plant.imu_flu();
        assert_eq!(plant.altitude(), 0.0);
        assert!((accel[2] - GRAVITY as f32).abs() < 1.0e-4);
    }

    #[test]
    fn symmetric_high_throttle_takes_off_without_tilting() {
        let mut plant = Quadrotor::default();
        for _ in 0..2_000 {
            plant.step([0.76; 4], 0.001);
        }
        assert!(plant.altitude() > 0.5);
        assert!(plant.euler()[0].abs() < 1.0e-8);
    }
}
