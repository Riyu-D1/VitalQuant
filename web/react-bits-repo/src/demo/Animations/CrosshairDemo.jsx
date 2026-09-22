import React, { useEffect, useMemo, useRef } from 'react';
import { Box, Button, Flex, Input, Text } from '@chakra-ui/react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';

import CodeExample from '../../components/code/CodeExample';
import Dependencies from '../../components/code/Dependencies';

import Customize from '../../components/common/Preview/Customize';
import PropTable from '../../components/common/Preview/PropTable';

import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import Crosshair from '../../content/Animations/Crosshair/Crosshair';
import { crosshair } from '../../constants/code/Animations/crosshairCode';

const DEFAULT_TEXT = 'Target';

const DEFAULT_PROPS = {
  color: '#ffffff',
  targeted: true
};

const CrosshairDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { color, targeted } = props;

  const [linkText, setLinkText] = React.useState(DEFAULT_TEXT);
  const linkRef = useRef(null);

  const containerRef = useRef(null);
  const [minWidth, setMinWidth] = React.useState(0);
  const hiddenRef = useRef(null);

  const propData = useMemo(
    () => [
      { name: 'color', type: 'string', default: "'white'", description: 'Color of the crosshair lines.' },
      {
        name: 'containerRef',
        type: 'RefObject<HTMLElement>',
        default: 'null',
        description:
          'Optional container ref to limit crosshair to specific element. If null, crosshair will be active on entire viewport.'
      }
    ],
    []
  );

  useEffect(() => {
    if (hiddenRef.current) {
      if (minWidth < hiddenRef.current.getBoundingClientRect().width) {
        setMinWidth(hiddenRef.current.getBoundingClientRect().width);
      }
    }
  }, [linkText, minWidth]);

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box ref={containerRef} position="relative" className="demo-container" minH={300} overflow="hidden">
            <Crosshair containerRef={targeted ? null : containerRef} color={color} />

            <Flex direction="column" justifyContent="center" alignItems="center">
              <Text
                _hover={{ color: 'magenta' }}
                transition=".3s ease"
                textAlign="center"
                fontWeight={900}
                fontSize={{ base: '2rem', md: '4rem' }}
                as="a"
                href="https://github.com/DavidHDev/react-bits"
                ref={linkRef}
                onMouseEnter={() => {
                  setLinkText('Locked');
                }}
                onMouseLeave={() => {
                  setLinkText(DEFAULT_TEXT);
                }}
                style={{ minWidth }}
              >
                {linkText}
              </Text>
              <Text position="relative" top="-10px" color="#444">
                (hover the text)
              </Text>
            </Flex>
            <Text
              ref={hiddenRef}
              style={{
                visibility: 'hidden',
                position: 'absolute',
                whiteSpace: 'nowrap',
                pointerEvents: 'none',
                overflow: 'hidden'
              }}
              aria-hidden="true"
              textAlign="center"
              fontWeight={900}
              fontSize={{ base: '2rem', md: '4rem' }}
            >
              {linkText}
            </Text>
          </Box>

          <Customize>
            <Flex gap={4} align="center" mt={4} mb={4}>
              <Text fontSize="sm">Crosshair Color</Text>
              <Input
                type="color"
                value={color}
                onChange={e => {
                  updateProp('color', e.target.value);
                }}
                width="60px"
                p={0}
              />
            </Flex>

            <Button
              fontSize="xs"
              bg="#170D27"
              borderRadius="10px"
              border="1px solid #271E37"
              _hover={{ bg: '#271E37' }}
              color="#fff"
              h={8}
              onClick={() => {
                updateProp('targeted', !targeted);
              }}
            >
              Cursor Container{' '}
              <Text color={targeted ? 'lightgreen' : 'coral'}>&nbsp;{targeted ? 'Viewport' : 'Targeted'}</Text>
            </Button>
          </Customize>

          <PropTable data={propData} />
          <Dependencies dependencyList={['gsap']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={crosshair} componentName="Crosshair" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default CrosshairDemo;
