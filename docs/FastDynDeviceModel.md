# FastDyn Device Model

FastDyn provides a flexible and high-performance framework for modeling devices in firmware analysis. Compared to frameworks like Avatar, FastDyn uses a **compositional device model**, which allows integration of models from multiple sources, including core QEMU, FastDyn-specific models, and frameworks such as HALucinator.

FastDyn currently supports several device models, each suited for different needs. Explore them below:

- [Classic Device Model](ClassicDeviceModel.md) – A simple, Avatar-like approach invoked on each I/O access, ideal for general-purpose modeling.  
- [Passthrough Device Model](PassthroughDeviceModel.md) – Provides direct access to device I/O and optimized for high performance.  
- [Elder Scroll Device Model](ElderScrollDeviceModel.md) – Builds on classic and passthrough models to automate device model generation using automata learning and machine learning.

