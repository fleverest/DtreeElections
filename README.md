
# [prefio](https://fleverest.github.io/prefio/)

# [elections.dtree](https://fleverest.github.io/elections.dtree): Audit ranked voting elections with Dirichlet-trees

Perform ballot-polling Bayesian audits for ranked voting elections using
a Dirichlet-tree prior distribution.

<!-- badges: start -->
[![CRAN_Status_Badge](https://www.r-pkg.org/badges/version/elections.dtree)](https://cran.r-project.org/package=elections.dtree)
[![pkgdown](https://github.com/fleverest/elections.dtree/workflows/pkgdown/badge.svg)](https://fleverest.github.io/elections.dtree/)
[![R-CMD-check](https://github.com/fleverest/elections.dtree/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/fleverest/elections.dtree/actions/workflows/R-CMD-check.yaml)
[![Codecov test
coverage](https://codecov.io/gh/fleverest/elections.dtree/branch/main/graph/badge.svg)](https://app.codecov.io/gh/fleverest/elections.dtree?branch=main)
<!-- badges: end -->

## Installation

#### CRAN

To install the latest minor release of `elections.dtree` from CRAN:

``` r
install.packages("elections.dtree")
```

#### GitHub

To install the development release of `elections.dtree` from GitHub:

``` r
# install.packages("remotes")
remotes::install_github("fleverest/elections.dtree")
```

## About the project

#### Why?

Bayesian audits of elections typically employ a Dirichlet prior,
conjugate to a multinomial distribution for the observed ballots. For
ranked voting systems, the number of ballot types can grow factorially.
In the case of instant-runoff voting (IRV), a popular type of ranked
voting system, the number of possible ballot types is `n!` with `n`
candidates (assuming all candidates are ranked; it is even greater if
partially completed ballots are permitted).

The Dirichlet distribution is a simple and effective choice if the
number of ballot types is small, but becomes problematic when this
number gets large. As `n` grows, the prior concentration parameters
(defined by `a0` in our implementation) need to be on the order of
`1 / n!` to ensure the prior is not overly informative. If `n` is large
enough, this may be smaller than the available precision. Also, the fact
that this varies by `n` is inconvenient. A more practical alternative is
given by the Dirichlet-tree distribution, which we implement in this
package.

The Dirichlet-tree distribution consists of nested Dirichlet
distributions, arranged hierarchically in a tree structure. The
structure represents the preference ordering of the candidates. Branches
coming out of each node correspond to choices for which candidate to
select as the next preferred, and nodes represent a ranking of
candidates (a complete ranking for leaf nodes, and an incomplete ranking
for internal nodes). We place a Dirichlet distribution at each node, to
model the conditional split of preferences at that node. The structure
as a whole then defines a Dirichlet-tree distribution. Just like the
Dirichlet, it is conjugate to a multinomial distribution. Also, the
Dirichlet-tree is a generalisation, including a Dirichlet as a special
case.

Using the Dirichlet-tree as a prior distribution allows it to scale
efficiently to large `n`, and does not require setting the concentration
parameters (`a0`) to values that depend on `n` to perform well. depend
on `n` to perform well.

#### How?

In this package, the tree structure is implemented such that the nodes
are only initialised when they appear in the observed ballot data
(i.e. when at least one of the ballots includes a preference sequence
that is represented by that node). This allows the memory-complexity to
be `O(n*m)`, where `m` is the number of ballots observed in the audit
process.

Without such lazy evaluation, the memory-complexity is necessarily
`O(n!)`. Hence, our implementation of the Dirichlet distribution (based
on a reducible Dirichlet-tree structure) enables a larger set of
candidates than would be possible using traditional methods.

To sample unseen ballots without loading all nodes into memory, this
repository implements a recursive strategy that generates samples
starting from any uninitialized point in the tree. This works well for
IRV ballot structures, since the permutation-tree structure is easily
navigated given a target ballot.

Currently, only IRV elections have been implemented, but other ranked
voting systems could be supported by implementing the corresponding
social choice function.

In order to support different tree structures or other elections which
deal with high cardinality, a similar recursive strategy for sampling
should be developed (and anyone designing a new tree structure should
consider this carefully).

## Usage

#### Load some data

Let’s start off by downloading some data from
[PrefLib](https://www.preflib.org/) using
[prefio](https://cran.r-project.org/package=prefio). In this example, we
select the Albury 2023 dataset containing the ballots cast in Albury for
the 2023 NSW Legislative Assembly Election. Other elections for the NSW
Legislative Assembly can be found
[here](https://www.preflib.org/dataset/00058).

``` r
albury <- prefio::read_preflib("nswla/00058-00000187.soi", from_preflib = TRUE)
head(albury)
```

We can see that in this election, voters could specify as few as one
candidate. Since the underlying structure for `prefio::preferences` is a
ranking matrix, we can easily find the length of the longest ballot as
follows:

``` r
max(albury$preferences, na.rm = TRUE)
```

In this election, the maximum ballot length was not bounded.

#### Specifying the prior

To work with `dirichlet_tree`s in R, there are two interfaces at your
disposal. The first is a standard S3 interface which all R users will be
familiar with. The second is an R6 interface which can be useful when
you would like to chain commands. Other than the ability to chain
commands, either interface is equivalent.

Here, we initialise a new Dirichlet-tree for the Albury 2023 election,
requiring exactly at least one preference specified per formal ballot
and using a prior `a0` parameter equal to `1.5`.

``` r
# S3 interface
dtree <- dirtree(
  candidates = names(albury$preferences),
  min_depth = 1,
  a0 = 1.5
)

# R6 interface (equivalent to S3)
dtree2 <- dirichlet_tree$new(
  candidates = names(albury$preferences),
  min_depth = 1,
  a0 = 1.5
)

dtree
```

#### Observing data

The data observed during the auditing process should be formatted as a
`prefio::preferences` object. Currently, our `albury` object has class
`prefio::aggregated_preferences`. We will also shuffle the ballots to
imitate a real auditing scenario where we sample the ballots at random.
To convert it to the appropriate format and shuffle we can do the
following:

``` r
ballots <- sample(prefio::as.preferences(albury))
```

Then to observe our first batch of say 10 ballots, we can update the
model to obtain our posterior:

``` r
# S3
update(dtree, ballots[1:10])

# R6
dtree2$update(ballots[1:10])

# R6 using chained commands
dtree2 <- dirichlet_tree$new(
  candidates = names(albury$preferences),
  min_depth = 1,
  a0 = 1.5
)$update(ballots[1:10])

dtree
```

#### Bayesian inference using the posterior Dirichlet-tree

To conduct Bayesian inference using the posterior we have just obtained,
we can employ monte-carlo simulation to simulate the unseen ballots and
determine the election outcome many times. This can be used to estimate
the probability that any particular candidate goes on to win the
election under the posterior distribution.

``` r
# S3
ps <- sample_posterior(dtree, n_elections = 1000, n_ballots = length(ballots))

# R6
ps <- dtree2$sample_posterior(n_elections = 1000, n_ballots = length(ballots))

# R6 using chained commands
ps <- dirichlet_tree$new(
  candidates = names(albury$preferences),
  min_depth = 1,
  a0 = 1.5
)$update(
  ballots[1:10]
)$sample_posterior(
  n_elections = 100,
  n_ballots = length(ballots)
)

ps
```

We can also take one realisation from the posterior distribution to
examine it directly, rather than automatically compute the simulated
election outcome and aggregate the results. To simulate the unobserved
`N = length(ballots) - 10` ballots from the posterior we can run the
following command:

``` r
# S3
simulated <- sample_predictive(dtree, n_ballots = length(ballots) - 10)

# R6
simulated <- dtree2$sample_predictive(n_ballots = length(ballots) - 10)

# R6 using chained commands
simulated <- dirichlet_tree$new(
  candidates = names(albury$preferences),
  min_depth = 1,
  a0 = 1.5
)$update(
  ballots[1:10]
)$sample_predictive(n_ballots = length(ballots) - 10)

head(simulated)
```

Then we can compute the (IRV) election outcome for this sample (plus the
observed data) explicitly like this:

``` r
social_choice(rbind(ballots[1:10], simulated), sc_function = "irv")
```
