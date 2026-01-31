# HCOMM C++ Gemini Collaboration Guide

This document outlines conventions and best practices for AI-assisted development in the HCOMM C++ codebase.

## Preferred Tools & Libraries

* HCOMM uses C++23, so that we can take the advantage of the modern C++.
* Prefer std library when available, use absl library when not

## Code Style & Conventions

* Comment in English, keep it concise but not simple.
* Use `///` for documentation comments, as we're writing C++.
* In documentation comments, focus on the function's purpose and design rationale rather than detailing each parameter.
  Avoid Doxygen-style tags like `@param`. If a parameter's functionality needs clarification, describe it within the
  main narrative of the comment.
* Only include headers that are actively used.
* Prefer explicit types over `std::pair` or `std::tuple` for return types.
* Consider Google C++ Style Guide if it's not mentioned above.

## About HCOMM

* HCOMM is a high performance communication library for C/S applications by exposing hardware capability as much as
  possible, and also optimization of domain oriented applications.
* Various protocol supports, HCOMM hides the complication of low level API, TCP/RDMA/UB etc
