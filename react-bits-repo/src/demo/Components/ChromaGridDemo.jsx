import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box } from '@chakra-ui/react';

import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import CodeExample from '../../components/code/CodeExample';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';

import { chromaGrid } from '../../constants/code/Components/chromaGridCode';
import ChromaGrid from '../../content/Components/ChromaGrid/ChromaGrid';

const DEFAULT_PROPS = {};

const ChromaGridDemo = () => {
  const { props, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);

  const propData = useMemo(
    () => [
      {
        name: 'items',
        type: 'Array',
        default: 'Demo []',
        description: 'Array of ChromaItem objects to display in the grid'
      },
      {
        name: 'className',
        type: 'string',
        default: "''",
        description: 'Additional CSS classes to apply to the grid container'
      },
      {
        name: 'radius',
        type: 'number',
        default: '300',
        description: 'Size of the spotlight effect in pixels'
      },
      {
        name: 'damping',
        type: 'number',
        default: '0.45',
        description: 'Cursor follow animation duration in seconds'
      },
      {
        name: 'fadeOut',
        type: 'number',
        default: '0.6',
        description: 'Fade-out animation duration in seconds when mouse leaves'
      },
      {
        name: 'ease',
        type: 'string',
        default: "'power3.out'",
        description: 'GSAP easing function for animations'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h="auto" overflow="hidden" p={0} py={6}>
            <ChromaGrid />
          </Box>

          <PropTable data={propData} />
          <Dependencies dependencyList={['gsap']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={chromaGrid} componentName="ChromaGrid" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default ChromaGridDemo;
