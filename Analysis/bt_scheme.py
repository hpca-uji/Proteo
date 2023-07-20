'''
Created on Oct 24, 2016

@author: David Llorens (dllorens@uji.es)
         (c) Universitat Jaume I 2016
@license: GPL2
'''
from abc import ABCMeta, abstractmethod
infinity = float("infinity")

## Esquema para BT básico --------------------------------------------------------------------------      

class PartialSolution(metaclass=ABCMeta):
    @abstractmethod
    def is_solution(self)-> "bool":
        pass
    
    @abstractmethod
    def get_solution(self)  -> "solution":
        pass
    
    @abstractmethod
    def successors(self) -> "IEnumerable<PartialSolution>":
        pass

class BacktrackingSolver(metaclass=ABCMeta):   
    @staticmethod
    def solve(initial_ps : "PartialSolution") -> "IEnumerable<Solution>":
        def bt(ps):
            if ps.is_solution():
                yield ps.get_solution()
            else:
                for new_ps in ps.successors():
                    yield from bt(new_ps)
                    
        yield from bt(initial_ps)

class BacktrackingSolverOld(metaclass=ABCMeta):   
    def solve(self, initial_ps : "PartialSolution") -> "IEnumerable<Solution>":
        def bt(ps):
            if ps.is_solution():
                return [ps.get_solution()]
            else:
                solutions = []
                for new_ps in ps.successors():
                    solutions.extend(bt(new_ps))
                return solutions
           
        return bt(initial_ps)
        
## Esquema para BT con control de visitados --------------------------------------------------------      

class PartialSolutionWithVisitedControl(PartialSolution):
    @abstractmethod
    def state(self)-> "state": 
        # the returned object must be of an inmutable type  
        pass

class BacktrackingVCSolver(metaclass=ABCMeta):
    @staticmethod   
    def solve(initial_ps : "PartialSolutionWithVisitedControl") -> "IEnumerable<Solution>":           
        def bt(ps):
            seen.add(ps.state())
            if ps.is_solution():
                yield ps.get_solution()
            else:
                for new_ps in ps.successors():
                    state = new_ps.state()
                    if state not in seen:
                        yield from bt(new_ps)
      
        seen = set()
        yield from bt(initial_ps)
        
## Esquema para BT para optimización ----------------------------------------------------------------      
        
class PartialSolutionWithOptimization(PartialSolutionWithVisitedControl):
    @abstractmethod
    def f(self)-> "int or double":   
        # result of applying the objective function to the partial solution
        pass

class BacktrackingOptSolver(metaclass=ABCMeta):   
    @staticmethod
    def solve(initial_ps : "PartialSolutionWithOptimization") -> "IEnumerable<Solution>":           
        def bt(ps):
            nonlocal best_solution_found_score
            ps_score = ps.f()
            best_seen[ps.state()] = ps_score
            if ps.is_solution() and ps_score < best_solution_found_score: #sólo muestra una solución si mejora la última mostrada
                best_solution_found_score = ps_score         
                yield ps.get_solution()
            else:
                for new_ps in ps.successors():
                    state = new_ps.state()
                    if state not in best_seen or new_ps.f() < best_seen[state]:
                        yield from bt(new_ps)
      
        best_seen = {}
        best_solution_found_score = infinity
        yield from bt(initial_ps)
