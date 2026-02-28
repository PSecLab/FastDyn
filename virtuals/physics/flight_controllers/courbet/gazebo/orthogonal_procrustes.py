import numpy as np
import itertools

import numpy as np
from dataclasses import dataclass
from typing import Literal, Optional


def rotation_to_axis_mapping(R: np.ndarray, tol: float = 1e-3) -> str:
    """
    Human-readable mapping if R is close to a signed permutation matrix.
    Example: X' = -Y, Y' = X, Z' = Z
    """
    R = np.asarray(R, dtype=float)
    names = ["X", "Y", "Z"]
    out = []
    for i in range(3):
        j = int(np.argmax(np.abs(R[i, :])))
        val = R[i, j]
        if not (np.isclose(abs(val), 1.0, atol=tol) and np.allclose(np.abs(R[i, :]).sum(), 1.0, atol=tol)):
            return "R is not close to a signed permutation; use Euler/quat to interpret."
        sign = "-" if val < 0 else ""
        out.append(f"{names[i]}' = {sign}{names[j]}")
    return ", ".join(out)


def all_axis_rotation_candidates():
    """
    Generate 24 proper rotations made of axis permutations and sign flips (90 x n deg rotations, -1 <= n <= 2) 
    with det=+1 (not reflections, only rotations)
    """
    mats = []
    axes = np.eye(3)
    for perm in itertools.permutations([0,1,2]):
        print(perm)
        P = axes[:, perm]
        for signs in itertools.product([1,-1], repeat=3):
            S = np.diag(signs)
            R = P @ S
            if round(np.linalg.det(R)) == 1:
                mats.append(R)

    # uniqueness check
    uniq_mats = []
    for R in mats:
        if not any(np.allclose(R, Q) for Q in uniq_mats):
            uniq_mats.append(R)
    return uniq_mats


@dataclass
class KabschFit:
    R: np.ndarray          # (3,3) rotation
    s: float               # scale (1.0 if disabled)
    b: np.ndarray          # (3,) bias/offset (0 if disabled)
    rmse: float            # fit error in same units as y
    detR: float            # determinant of R (should be +1)
    x_mean: np.ndarray     # (3,)
    y_mean: np.ndarray     # (3,)


def kabsch_fit(
    x: np.ndarray,
    y: np.ndarray,
    *,
    estimate_scale: bool = True,
    estimate_bias: bool = True,
    enforce_so3: bool = True,
    weights: Optional[np.ndarray] = None,
    brute_force: bool = False
) -> KabschFit:
    """
    Fit y ≈ s * R * x + b using Kabsch/Orthogonal Procrustes (SVD).

    Args:
        x: (N,3) source vectors (e.g., Gazebo mag)
        y: (N,3) target vectors (e.g., ArduPilot mag)
        estimate_scale: fit scalar s (useful for unit mismatch)
        estimate_bias: fit offset b (hard-iron / software offset)
        enforce_so3: force det(R)=+1 (disallow reflection)
        weights: optional (N,) nonnegative weights
        brute_force: if True, try 24 axis-aligned rotations and pick best fit

    Returns:
        KabschFit with R,s,b and rmse.
    """
    if brute_force:
        Rs = all_axis_rotation_candidates()
        best = None
        for R in Rs:
            s, b, rmse, detR, x_mean, y_mean = kabsch_fit_direct(x, y, R, fix_scale=not estimate_scale, fix_bias=not estimate_bias)
            if best is None or rmse < best["rmse"]:
                best = {"R": R, "s": s, "b": b, "rmse": rmse, "detR": detR, "x_mean": x_mean, "y_mean": y_mean}
        return KabschFit(R=best['R'], s=best['s'], b=best['b'], rmse=best['rmse'], detR=best['detR'], x_mean=best['x_mean'], y_mean=best['y_mean'])

    else:
        x = np.asarray(x, dtype=float)
        y = np.asarray(y, dtype=float)
        if x.ndim != 2 or y.ndim != 2 or x.shape != y.shape or x.shape[1] != 3:
            raise ValueError("x and y must be (N,3) arrays with the same shape.")
        N = x.shape[0]
        if N < 3:
            raise ValueError("Need at least 3 paired samples.")

        if weights is not None:
            w = np.asarray(weights, dtype=float).reshape(-1)
            if w.shape[0] != N:
                raise ValueError("weights must be shape (N,).")
            if np.any(w < 0):
                raise ValueError("weights must be nonnegative.")
            ws = w.sum()
            if ws <= 0:
                raise ValueError("weights must sum to > 0.")
            w = w / ws
        else:
            w = None

        # Means (weighted if provided)
        if estimate_bias:
            if w is None:
                x_mean = x.mean(axis=0)
                y_mean = y.mean(axis=0)
            else:
                x_mean = (w[:, None] * x).sum(axis=0)
                y_mean = (w[:, None] * y).sum(axis=0)
        else:
            x_mean = np.zeros(3)
            y_mean = np.zeros(3)

        Xc = x - x_mean
        Yc = y - y_mean

        # Cross-covariance H = sum_k (Yc_k^T Xc_k) = Yc^T Xc
        if w is None:
            H = Yc.T @ Xc
        else:
            H = (Yc * w[:, None]).T @ Xc

        # SVD
        U, S, Vt = np.linalg.svd(H)
        R = U @ Vt

        # Enforce proper rotation (det=+1)
        if enforce_so3 and np.linalg.det(R) < 0:
            U2 = U.copy()
            U2[:, -1] *= -1.0
            R = U2 @ Vt

        detR = float(np.linalg.det(R))

        # Scale (optional). Solve s = argmin ||Yc - s*(R Xc)||_F
        if estimate_scale:
            RX = (R @ Xc.T).T  # (N,3)
            if w is None:
                num = np.sum(Yc * RX)
                den = np.sum(RX * RX)
            else:
                num = np.sum(w[:, None] * (Yc * RX))
                den = np.sum(w[:, None] * (RX * RX))
            s = float(num / den) if den > 0 else 1.0
        else:
            s = 1.0

        # Bias (optional): b = y_mean - s R x_mean
        if estimate_bias:
            b = y_mean - s * (R @ x_mean)
        else:
            b = np.zeros(3)

        # RMSE
        y_hat = (s * (R @ x.T)).T + b
        err = y - y_hat
        if w is None:
            rmse = float(np.sqrt(np.mean(np.sum(err**2, axis=1))))
        else:
            rmse = float(np.sqrt(np.sum(w * np.sum(err**2, axis=1))))

        return KabschFit(R=R, s=s, b=b, rmse=rmse, detR=detR, x_mean=x_mean, y_mean=y_mean)


def kabsch_fit_direct(x, y, R, *, fix_scale=False, fix_bias=False):
    """
    Fit scale s and bias b given rotation R such that y ≈ s R x + b
    Args:
    - x: Nx3
    - y: Nx3
    - R: 3x3 rotation matrix with det=+1
    Returns:
    - s: scalar
    - b: vector offset 3x1
    - rmse: root mean square error of the fit
    """
    # rotate x and construct Nx3 matrix
    X = (R @ x.T).T # rotate x
    detR = float(np.linalg.det(R))
    
    if fix_scale and fix_bias:
        s = 1.0
        b = np.zeros(3)
        err = y - (s * X + b)
        rmse = np.sqrt((err**2).mean())
        x_mean = X.mean(axis=0, keepdims=True)
        y_mean = y.mean(axis=0, keepdims=True)
        return s, b, rmse, detR, x_mean, y_mean
    
    elif fix_scale and not fix_bias:
        s = 1.0
        b = y.mean(axis=0) - s * X.mean(axis=0)
        err = y - (s * X + b)
        rmse = np.sqrt((err**2).mean())
        x_mean = X.mean(axis=0, keepdims=True)
        y_mean = y.mean(axis=0, keepdims=True)
        return s, b, rmse, detR, x_mean, y_mean
    
    elif not fix_scale and fix_bias:
        b = np.zeros(3)
        # Solve for s in y - b = sX
        num = ((y - b) * X).sum()
        den = (X * X).sum()
        s = num / den if den > 0 else 1.0
        err = y - (s * X + b)
        rmse = np.sqrt((err**2).mean())
        x_mean = X.mean(axis=0, keepdims=True)
        y_mean = y.mean(axis=0, keepdims=True)
        return s, b, rmse, detR, x_mean, y_mean
    
    else:
        # Use scalar s: minimize ||y - sX - b||; closed form:
        Xc = X - X.mean(axis=0, keepdims=True)
        yc = y - y.mean(axis=0, keepdims=True)
        s = (yc * Xc).sum() / (Xc * Xc).sum()
        b = y.mean(axis=0) - s * X.mean(axis=0)
        err = y - (s * X + b)
        rmse = np.sqrt((err**2).mean())
        x_mean = X.mean(axis=0, keepdims=True)
        y_mean = y.mean(axis=0, keepdims=True)
        return s, b, rmse, detR, x_mean, y_mean


def apply_kabsch(x: np.ndarray, fit: KabschFit) -> np.ndarray:
    """Apply y = s R x + b to an (N,3) array x."""
    x = np.asarray(x, dtype=float)
    return (fit.s * (fit.R @ x.T)).T + fit.b

# Example usage:
# x: Nx3 (gazebo), y: Nx3 (ardupilot)
# best = identify_transform(x, y)
# print(best["R"], best["s"], best["b"], best["rmse"])

if __name__ == "__main__":
    # Example data
    x = np.array([
        [1.0, 2.0, 3.0],
        [4.0, 5.0, 6.0],
        [7.0, 8.0, 9.0],
    ])
    # Apply known transform for testing
    R_true = np.array([[0, -1, 0],
                       [1, 0, 0],
                       [0, 0, 1]])
    s_true = 1.0
    b_true = np.array([0.0, 0.0, 0.0])
    y = s_true * (R_true @ x.T).T + b_true

    best = kabsch_fit(x, y, brute_force=False, estimate_scale=False, estimate_bias=False)
    print("Identified Transform:")
    print("R:\n", best.R)
    print("s:", best.s)
    print("b:", best.b)
    print("RMSE:", best.rmse)
